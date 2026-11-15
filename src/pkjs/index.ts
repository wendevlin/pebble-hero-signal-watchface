/*
 * Phone-side PebbleKit JS for Hero Signal.
 *
 * - Settings page (src/pkjs/config.html) and pushing the settings to the watch.
 * - Outdoor weather from the phone's location via Open-Meteo (no API key).
 * - Optionally a second temperature from Home Assistant (a `weather.*` entity
 *   or a temperature sensor), shown on the watch next to it: "12°/21°".
 *
 * The WATCH drives the refresh cadence (it sends REFRESH every 30 minutes and
 * whenever the phone reconnects): phone-side timers stall while the Pebble app
 * is in the background, but an inbound AppMessage always wakes it.
 *
 * Authored in TypeScript, bundled to src/pkjs/index.js by `bun run build:pkjs`.
 * Plain `function`s and no `?.`/`??` on purpose: the bundle is not down-levelled
 * and must run in the Pebble app's JS runtime.
 */

import CONFIG_PAGE from "./config.html" with { type: "text" };
import {
  HA_CLIENT_ID,
  HA_REDIRECT_URI,
  interpretLoginFlow,
  loginFlowCredentialsBody,
  loginFlowMfaBody,
  loginFlowStartBody,
  normalizeHaUrl,
  tokenExchangeBody,
  tokenRefreshBody,
  type LoginStep,
} from "./ha";
import {
  defaultTempEntity,
  parseHaTemp,
  parseOpenMeteo,
  tempEntitiesFromStates,
  type HaStateObject,
  type TempEntity,
  type WeatherReading,
} from "./weather";

// A text import yields the file as a string at runtime; bun's typings declare
// *.html imports as HTML bundles, hence the cast.
const CONFIG_HTML = CONFIG_PAGE as unknown as string;

const LOG = "Hero Signal: ";
const POSITION_MAX_AGE_MS = 30 * 60 * 1000;
// Number of skylines (City in src/c/skyline.h, CITIES in config.html).
const SKYLINE_COUNT = 5;

// HA_TEMP value that switches the Home Assistant temperature off on the watch.
const HA_TEMP_NONE = -32768;

// --- Settings -----------------------------------------------------------------

interface Settings {
  hero: number;
  /** Skyline, see City in src/c/skyline.h. */
  skyline: number;
  fahrenheit: boolean;
  stepGoal: number;
  vibe: boolean;
  /** Home Assistant entity for the second temperature; "" = off. */
  haEntity: string;
}

const DEFAULTS: Settings = {
  hero: 0,
  skyline: 0,
  fahrenheit: false,
  stepGoal: 8000,
  vibe: true,
  haEntity: "",
};

function loadSettings(): Settings {
  const out: Settings = {
    hero: DEFAULTS.hero,
    skyline: DEFAULTS.skyline,
    fahrenheit: DEFAULTS.fahrenheit,
    stepGoal: DEFAULTS.stepGoal,
    vibe: DEFAULTS.vibe,
    haEntity: DEFAULTS.haEntity,
  };
  try {
    const raw = JSON.parse(localStorage.getItem("settings") || "{}");
    return sanitize(raw, out);
  } catch (e) {
    return out;
  }
}

function sanitize(raw: Partial<Settings>, base: Settings): Settings {
  const hero = Number(raw.hero);
  const skyline = Number(raw.skyline);
  const goal = Number(raw.stepGoal);
  return {
    hero: hero >= 0 && hero <= 3 ? Math.floor(hero) : base.hero,
    skyline: skyline >= 0 && skyline < SKYLINE_COUNT ? Math.floor(skyline) : base.skyline,
    fahrenheit: typeof raw.fahrenheit === "boolean" ? raw.fahrenheit : base.fahrenheit,
    stepGoal: goal >= 500 && goal <= 100000 ? Math.floor(goal) : base.stepGoal,
    vibe: typeof raw.vibe === "boolean" ? raw.vibe : base.vibe,
    haEntity: typeof raw.haEntity === "string" ? raw.haEntity : base.haEntity,
  };
}

function saveSettings(s: Settings): void {
  localStorage.setItem("settings", JSON.stringify(s));
}

// --- Outbound AppMessages, one at a time -------------------------------------
// A second message sent before the watch acked the first can be dropped, so
// messages are queued and each waits for the previous ack/nack (or a timeout).

interface Outbound {
  label: string;
  message: { [key: string]: number | string };
}
const outbox: Outbound[] = [];
let outboxBusy = false;

function pumpOutbox(): void {
  if (outboxBusy) return;
  const next = outbox.shift();
  if (!next) return;
  outboxBusy = true;
  let finished = false;
  const done = function () {
    if (finished) return;
    finished = true;
    outboxBusy = false;
    pumpOutbox();
  };
  setTimeout(function () {
    if (!finished) console.log(LOG + "no ack for " + next.label);
    done();
  }, 10000);
  Pebble.sendAppMessage(next.message, done, function (e) {
    console.log(LOG + "failed to send " + next.label + ": " + JSON.stringify(e));
    done();
  });
}

function send(label: string, message: { [key: string]: number | string }): void {
  outbox.push({ label: label, message: message });
  pumpOutbox();
}

function sendSettings(): void {
  const s = loadSettings();
  send("settings", {
    HERO_COLOR: s.hero,
    SKYLINE: s.skyline,
    FAHRENHEIT: s.fahrenheit ? 1 : 0,
    STEP_GOAL: s.stepGoal,
    VIBE_DISCONNECT: s.vibe ? 1 : 0,
  });
}

function sendWeather(w: WeatherReading): void {
  console.log(LOG + "weather " + w.tenthsC / 10 + "°C cond " + w.cond + " day " + w.isDay);
  send("weather", { WEATHER_TEMP: w.tenthsC, WEATHER_COND: w.cond, IS_DAY: w.isDay ? 1 : 0 });
}

function sendHaTemp(tenthsC: number): void {
  send("Home Assistant temperature", { HA_TEMP: tenthsC });
}

// --- HTTP ---------------------------------------------------------------------

function request(
  method: string,
  url: string,
  headers: { [k: string]: string },
  body: string | null,
  cb: (status: number, text: string) => void,
): void {
  const xhr = new XMLHttpRequest();
  xhr.open(method, url, true);
  for (const k in headers) xhr.setRequestHeader(k, headers[k]);
  xhr.timeout = 20000;
  xhr.onload = function () {
    cb(xhr.status, xhr.responseText);
  };
  xhr.onerror = function () {
    cb(0, "");
  };
  xhr.ontimeout = function () {
    cb(0, "");
  };
  xhr.send(body);
}

function parseJson(text: string): unknown {
  try {
    return JSON.parse(text);
  } catch (e) {
    return null;
  }
}

// --- Weather: phone location (Open-Meteo) ------------------------------------

function fetchLocationWeather(): void {
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      localStorage.setItem("lastLat", String(pos.coords.latitude));
      localStorage.setItem("lastLon", String(pos.coords.longitude));
      fetchOpenMeteo(pos.coords.latitude, pos.coords.longitude);
    },
    function (err) {
      // In the background the phone often refuses a fresh fix; the last one is
      // plenty accurate for weather.
      const lat = parseFloat(localStorage.getItem("lastLat") || "");
      const lon = parseFloat(localStorage.getItem("lastLon") || "");
      if (isFinite(lat) && isFinite(lon)) {
        fetchOpenMeteo(lat, lon);
      } else {
        console.log(LOG + "no location: " + JSON.stringify(err));
      }
    },
    { timeout: 15000, maximumAge: POSITION_MAX_AGE_MS },
  );
}

function fetchOpenMeteo(lat: number, lon: number): void {
  const url =
    "https://api.open-meteo.com/v1/forecast?latitude=" + lat.toFixed(3) +
    "&longitude=" + lon.toFixed(3) +
    "&current=temperature_2m,weather_code,is_day&timezone=auto";
  request("GET", url, {}, null, function (status, text) {
    const reading = status === 200 ? parseOpenMeteo(parseJson(text)) : null;
    if (reading) {
      sendWeather(reading);
    } else {
      console.log(LOG + "Open-Meteo failed (" + status + ")");
    }
  });
}

// --- Home Assistant -----------------------------------------------------------

function haConnected(): boolean {
  return !!localStorage.getItem("haUrl") && !!localStorage.getItem("haAccessToken");
}

function clearHa(): void {
  ["haUrl", "haAccessToken", "haRefreshToken", "haExpiresAt", "haEntities", "haError"].forEach(function (k) {
    localStorage.removeItem(k);
  });
}

function cachedEntities(): TempEntity[] {
  const list = parseJson(localStorage.getItem("haEntities") || "[]");
  return Array.isArray(list) ? (list as TempEntity[]) : [];
}

function storeToken(token: { access_token?: string; refresh_token?: string; expires_in?: number }): void {
  localStorage.setItem("haAccessToken", String(token.access_token));
  if (token.refresh_token) localStorage.setItem("haRefreshToken", token.refresh_token);
  const ttl = Number(token.expires_in) || 1800;
  localStorage.setItem("haExpiresAt", String(Date.now() + ttl * 1000));
}

/** Calls back with a usable access token (refreshing it first if it is about to expire). */
function withHaToken(cb: (token: string | null) => void, forceRefresh?: boolean): void {
  const url = localStorage.getItem("haUrl") || "";
  const refresh = localStorage.getItem("haRefreshToken") || "";
  const expiresAt = parseInt(localStorage.getItem("haExpiresAt") || "0", 10);
  if (!forceRefresh && expiresAt - Date.now() > 60000) {
    cb(localStorage.getItem("haAccessToken"));
    return;
  }
  if (!url || !refresh) {
    cb(localStorage.getItem("haAccessToken"));
    return;
  }
  request(
    "POST",
    url + "/auth/token",
    { "Content-Type": "application/x-www-form-urlencoded" },
    tokenRefreshBody(refresh, HA_CLIENT_ID),
    function (status, text) {
      const token = parseJson(text) as { access_token?: string } | null;
      if (status === 200 && token && token.access_token) {
        storeToken(token);
        cb(token.access_token);
      } else {
        console.log(LOG + "HA token refresh failed (" + status + ")");
        cb(null);
      }
    },
  );
}

/** GET a Home Assistant API path as JSON, retrying once with a fresh token on 401. */
function haGet(path: string, cb: (data: unknown) => void): void {
  const url = localStorage.getItem("haUrl") || "";
  const attempt = function (token: string | null, retry: boolean) {
    if (!url || !token) {
      cb(null);
      return;
    }
    request("GET", url + path, { Authorization: "Bearer " + token }, null, function (status, text) {
      if (status === 401 && retry) {
        withHaToken(function (fresh) {
          attempt(fresh, false);
        }, true);
        return;
      }
      cb(status === 200 ? parseJson(text) : null);
    });
  };
  withHaToken(function (token) {
    attempt(token, true);
  });
}

function fetchHaEntities(cb: (list: TempEntity[]) => void): void {
  haGet("/api/states", function (states) {
    // Keep the cached list when Home Assistant is unreachable.
    if (!Array.isArray(states)) {
      cb(cachedEntities());
      return;
    }
    const list = tempEntitiesFromStates(states);
    localStorage.setItem("haEntities", JSON.stringify(list));
    cb(list);
  });
}

function haTempActive(): boolean {
  return haConnected() && !!loadSettings().haEntity;
}

/** Fetch the chosen Home Assistant temperature and forward it to the watch. */
function refreshHaTemp(): void {
  if (!haTempActive()) return;
  const entity = loadSettings().haEntity;
  haGet("/api/states/" + encodeURIComponent(entity), function (data) {
    const tenths = parseHaTemp(data as HaStateObject | null);
    if (tenths === null) {
      // The watch keeps the last value and hides it once it is 3 hours old.
      console.log(LOG + "no Home Assistant temperature from " + entity);
      return;
    }
    console.log(LOG + "Home Assistant " + entity + " " + tenths / 10 + "°C");
    sendHaTemp(tenths);
  });
}

function refreshWeather(): void {
  fetchLocationWeather();
  refreshHaTemp();
}

// --- Settings page -------------------------------------------------------------

function openConfig(): void {
  const s = loadSettings();
  const config = {
    hero: s.hero,
    skyline: s.skyline,
    previewBase: HA_CLIENT_ID + "previews/",
    fahrenheit: s.fahrenheit,
    stepGoal: s.stepGoal,
    vibe: s.vibe,
    ha: {
      connected: haConnected(),
      url: localStorage.getItem("haUrl") || localStorage.getItem("haLastUrl") || "",
      user: localStorage.getItem("haLastUser") || "",
      entity: s.haEntity,
      entities: cachedEntities(),
      // Shown once: the outcome of the last login attempt.
      error: localStorage.getItem("haError") || "",
      justConnected: localStorage.getItem("haJustConnected") === "1",
    },
  };
  localStorage.removeItem("haError");
  localStorage.removeItem("haJustConnected");
  const html = CONFIG_HTML.replace("__CONFIG__", JSON.stringify(config));
  Pebble.openURL("data:text/html;charset=utf-8," + encodeURIComponent(html));
}

function haLoginFailed(url: string, message: string): void {
  console.log(LOG + "Home Assistant login failed: " + message);
  localStorage.setItem("haError", "Couldn't log in to " + url + ": " + message + ".");
  showLoginOutcome();
}

/**
 * Bring the settings back with the login outcome. The Pebble app may ignore a
 * page opened by PebbleKit JS after the settings closed; then the outcome is
 * shown the next time the settings open (it's kept until then).
 */
function showLoginOutcome(): void {
  try {
    openConfig();
  } catch (e) {
    console.log(LOG + "could not reopen the settings: " + e);
  }
}

/** POST JSON to a login-flow URL and interpret the answer. */
function loginFlowPost(url: string, body: string, cb: (step: LoginStep) => void): void {
  request("POST", url, { "Content-Type": "application/json" }, body, function (status, text) {
    cb(interpretLoginFlow(status, parseJson(text)));
  });
}

/**
 * Log in through Home Assistant's login-flow API with the username, password
 * (and two-factor code) typed on the settings page; see src/pkjs/ha.ts for
 * why this replaces HA's login web page. Ends with regular OAuth tokens.
 */
function haPasswordLogin(rawUrl: string, username: string, password: string, mfa: string): void {
  const url = normalizeHaUrl(rawUrl);
  if (!url || !username || !password) return;
  localStorage.setItem("haLastUrl", url);
  localStorage.setItem("haLastUser", username);
  loginFlowPost(url + "/auth/login_flow", loginFlowStartBody(HA_CLIENT_ID, HA_REDIRECT_URI), function (start) {
    if (start.kind !== "step") {
      haLoginFailed(url, start.kind === "error" ? start.message : "unexpected answer from Home Assistant");
      return;
    }
    const flowUrl = url + "/auth/login_flow/" + encodeURIComponent(start.flowId);
    loginFlowPost(flowUrl, loginFlowCredentialsBody(username, password, HA_CLIENT_ID), function (step) {
      if (step.kind === "code") {
        exchangeHaCode(url, step.code);
      } else if (step.kind === "step" && step.stepId === "mfa") {
        if (!mfa) {
          haLoginFailed(url, "this account uses two-factor authentication, enter the current code too");
          return;
        }
        loginFlowPost(flowUrl, loginFlowMfaBody(mfa, HA_CLIENT_ID), function (last) {
          if (last.kind === "code") exchangeHaCode(url, last.code);
          else haLoginFailed(url, last.kind === "error" ? last.message : "unexpected answer from Home Assistant");
        });
      } else {
        haLoginFailed(url, step.kind === "error" ? step.message : "unexpected login step " + step.stepId);
      }
    });
  });
}

/** Trade a login-flow code for tokens, then pick a sensor and push its temperature. */
function exchangeHaCode(url: string, code: string): void {
  request(
    "POST",
    url + "/auth/token",
    { "Content-Type": "application/x-www-form-urlencoded" },
    tokenExchangeBody(code, HA_CLIENT_ID, HA_REDIRECT_URI),
    function (status, text) {
      const token = parseJson(text) as { access_token?: string } | null;
      if (status !== 200 || !token || !token.access_token) {
        haLoginFailed(url, "token exchange failed (" + status + ")");
        return;
      }
      clearHa();
      localStorage.setItem("haUrl", url);
      storeToken(token);
      fetchHaEntities(function (list) {
        // Pre-select an entity (a weather entity if there is one) unless one is chosen.
        const s = loadSettings();
        if (!s.haEntity) s.haEntity = defaultTempEntity(list);
        saveSettings(s);
        console.log(LOG + "Home Assistant connected, " + list.length + " entities");
        localStorage.setItem("haJustConnected", "1");
        refreshWeather();
        showLoginOutcome();
      });
    },
  );
}

// --- Events ----------------------------------------------------------------------

Pebble.addEventListener("ready", function () {
  sendSettings();
  refreshWeather();
});

Pebble.addEventListener("appmessage", function (e) {
  if (e && e.payload && e.payload.REFRESH !== undefined) refreshWeather();
});

Pebble.addEventListener("showConfiguration", function () {
  openConfig();
  // Refresh the entity list in the background for next time.
  if (haConnected()) fetchHaEntities(function () {});
});

Pebble.addEventListener("webviewclosed", function (e) {
  if (!e || !e.response) return;
  const msg = parseJson(decodeURIComponent(e.response)) as {
    action?: string;
    settings?: Partial<Settings>;
    haDisconnect?: boolean;
    url?: string;
    username?: string;
    password?: string;
    mfa?: string;
  } | null;
  if (!msg) return;

  if (msg.settings) saveSettings(sanitize(msg.settings, loadSettings()));

  if (msg.action === "ha_login") {
    sendSettings();  // the rest of the page was saved above
    haPasswordLogin(msg.url || "", msg.username || "", msg.password || "", msg.mfa || "");
    return;
  }
  if (msg.haDisconnect) clearHa();
  sendSettings();
  if (!haTempActive()) sendHaTemp(HA_TEMP_NONE);
  refreshWeather();
});
