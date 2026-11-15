/*
 * Pure helpers for logging in to Home Assistant (phone-side). Unit-tested in
 * tests/ha.test.ts.
 *
 * Why not Home Assistant's login web page: the Pebble app's settings webview
 * blocks web storage, and HA's login page touches localStorage first thing,
 * so its script dies and only the logo shows (reproduced in Chromium with
 * storage access throwing). Instead the phone talks to HA's login-flow API,
 * the same API that login page uses:
 *
 *   POST /auth/login_flow            {client_id, handler, redirect_uri} -> flow_id
 *   POST /auth/login_flow/<flow_id>  {username, password, client_id}    -> code
 *                                    (or a "mfa" step: {code, client_id} -> code)
 *   POST /auth/token                 authorization_code -> access + refresh token
 *
 * So the result is the same OAuth tokens as a browser login, no long-lived
 * token. The password only goes to Home Assistant and is never stored.
 *
 * HA identifies the app by `client_id` (a URL) and requires `redirect_uri` on
 * the same host; both point at this repo's GitHub Pages site (docs/).
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

// --- Login flow ---------------------------------------------------------------

/** Body that starts a login flow with HA's built-in username/password provider. */
export function loginFlowStartBody(clientId: string, redirectUri: string): string {
  return JSON.stringify({ client_id: clientId, handler: ["homeassistant", null], redirect_uri: redirectUri });
}

export function loginFlowCredentialsBody(username: string, password: string, clientId: string): string {
  return JSON.stringify({ username: username, password: password, client_id: clientId });
}

export function loginFlowMfaBody(code: string, clientId: string): string {
  return JSON.stringify({ code: code, client_id: clientId });
}

/** What a login-flow response means for the next step. */
export type LoginStep =
  | { kind: "code"; code: string }
  | { kind: "step"; flowId: string; stepId: string }
  | { kind: "error"; message: string };

const FLOW_ERRORS: { [code: string]: string } = {
  invalid_auth: "wrong username or password",
  invalid_code: "wrong two-factor code",
  login_expired: "the login took too long, try again",
};

/** Interpret an HTTP status + body from /auth/login_flow(/<id>). */
export function interpretLoginFlow(status: number, body: unknown): LoginStep {
  if (status === 0) return { kind: "error", message: "Home Assistant is not reachable from the phone" };
  if (status === 403) return { kind: "error", message: "Home Assistant refused the login (IP banned or blocked)" };
  if (status === 404) return { kind: "error", message: "no Home Assistant login found at this address" };
  const r = (body || {}) as {
    type?: string;
    result?: unknown;
    flow_id?: string;
    step_id?: string;
    errors?: { base?: string } | null;
    reason?: string;
    message?: string;
  };
  if (status !== 200) {
    return { kind: "error", message: "Home Assistant answered " + status + (r.message ? " (" + r.message + ")" : "") };
  }
  if (r.type === "create_entry" && typeof r.result === "string" && r.result) {
    return { kind: "code", code: r.result };
  }
  if (r.type === "form" && r.flow_id) {
    const base = r.errors && r.errors.base;
    if (base) return { kind: "error", message: FLOW_ERRORS[base] || base };
    return { kind: "step", flowId: r.flow_id, stepId: r.step_id || "" };
  }
  if (r.type === "abort") return { kind: "error", message: "login aborted (" + (r.reason || "unknown") + ")" };
  return { kind: "error", message: "unexpected answer from Home Assistant" };
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
