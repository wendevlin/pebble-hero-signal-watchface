/*
 * Pure helpers for the Home Assistant OAuth flow (phone-side), adapted from
 * the Casita watchface. Unit-tested in tests/ha.test.ts.
 *
 * Home Assistant uses IndieAuth-style OAuth2: `client_id` is a URL that
 * identifies the app, and `redirect_uri` must be on the same host. Both live on
 * this repo's GitHub Pages site (served from /docs):
 *   - client_id:    the site root (docs/index.html, also the future docs home)
 *   - redirect_uri: docs/oauth/callback.html, which bounces the auth code back
 *                   into the Pebble app via `pebblejs://close#...`
 * The callback URL is baked into every released .pbw: don't move that file.
 *
 * The login starts IN the settings webview (config.html navigates to HA's
 * authorize page itself; the Pebble app does not let PebbleKit JS open a new
 * page after the settings closed). config.html therefore carries its own copy
 * of base64UrlEncode and the state format: keep the two in sync.
 */

export const HA_CLIENT_ID = "https://wendevlin.github.io/pebble-hero-signal-watchface/";
export const HA_REDIRECT_URI =
  "https://wendevlin.github.io/pebble-hero-signal-watchface/oauth/callback.html";

/**
 * Normalize a user-entered Home Assistant URL like Casita does: trim, default
 * to https:// and strip trailing slashes. Returns "" for empty input.
 * config.html has a copy: keep them in sync.
 */
export function normalizeHaUrl(input: string): string {
  let url = (input || "").trim();
  if (url === "") return "";
  if (!/^https?:\/\//i.test(url)) url = "https://" + url;
  return url.replace(/\/+$/, "");
}

// --- base64url over UTF-8 (no btoa/TextEncoder in every runtime) ------------

const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

function utf8Bytes(str: string): number[] {
  const enc = encodeURIComponent(str);
  const bytes: number[] = [];
  for (let i = 0; i < enc.length; i++) {
    if (enc.charAt(i) === "%") {
      bytes.push(parseInt(enc.substr(i + 1, 2), 16));
      i += 2;
    } else {
      bytes.push(enc.charCodeAt(i));
    }
  }
  return bytes;
}

function bytesToUtf8(bytes: number[]): string {
  let enc = "";
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    enc += b < 16 ? "%0" + b.toString(16) : "%" + b.toString(16);
  }
  return decodeURIComponent(enc);
}

export function base64UrlEncode(str: string): string {
  const bytes = utf8Bytes(str);
  let out = "";
  for (let i = 0; i < bytes.length; i += 3) {
    const b0 = bytes[i];
    const b1 = i + 1 < bytes.length ? bytes[i + 1] : 0;
    const b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
    out += B64.charAt(b0 >> 2);
    out += B64.charAt(((b0 & 3) << 4) | (b1 >> 4));
    out += i + 1 < bytes.length ? B64.charAt(((b1 & 15) << 2) | (b2 >> 6)) : "";
    out += i + 2 < bytes.length ? B64.charAt(b2 & 63) : "";
  }
  return out;
}

export function base64UrlDecode(str: string): string {
  const bytes: number[] = [];
  let buffer = 0;
  let bits = 0;
  for (let i = 0; i < str.length; i++) {
    const idx = B64.indexOf(str.charAt(i));
    if (idx < 0) continue;
    buffer = (buffer << 6) | idx;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes.push((buffer >> bits) & 0xff);
    }
  }
  return bytesToUtf8(bytes);
}

// --- OAuth ------------------------------------------------------------------

export interface HaState {
  haUrl: string;
  nonce: string;
  /** Settings carried by states from older builds ({u, n, s}); null otherwise. */
  settings: unknown;
}

/**
 * Encode the HA URL and a random nonce into an opaque, URL-safe state param
 * (base64url(JSON {u, n}), the same format as Casita).
 */
export function encodeState(haUrl: string, nonce: string): string {
  return base64UrlEncode(JSON.stringify({ u: haUrl, n: nonce }));
}

/** Decode a state param produced by encodeState. Throws on malformed input. */
export function decodeState(state: string): HaState {
  const obj = JSON.parse(base64UrlDecode(state));
  return {
    haUrl: String(obj.u || ""),
    nonce: String(obj.n || ""),
    settings: obj.s === undefined ? null : obj.s,
  };
}

export function buildAuthorizeUrl(
  haUrl: string,
  clientId: string,
  redirectUri: string,
  state: string,
): string {
  return (
    haUrl +
    "/auth/authorize?client_id=" + encodeURIComponent(clientId) +
    "&redirect_uri=" + encodeURIComponent(redirectUri) +
    "&response_type=code" +
    "&state=" + encodeURIComponent(state)
  );
}

export function tokenExchangeBody(code: string, clientId: string, redirectUri: string): string {
  return (
    "grant_type=authorization_code" +
    "&code=" + encodeURIComponent(code) +
    "&client_id=" + encodeURIComponent(clientId) +
    "&redirect_uri=" + encodeURIComponent(redirectUri)
  );
}

export function tokenRefreshBody(refreshToken: string, clientId: string): string {
  return (
    "grant_type=refresh_token" +
    "&refresh_token=" + encodeURIComponent(refreshToken) +
    "&client_id=" + encodeURIComponent(clientId)
  );
}
