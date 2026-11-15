import { describe, expect, test } from "bun:test";
import { readFileSync } from "node:fs";
import {
  HA_CLIENT_ID,
  HA_REDIRECT_URI,
  interpretLoginFlow,
  loginFlowCredentialsBody,
  loginFlowStartBody,
  normalizeHaUrl,
  tokenExchangeBody,
} from "../src/pkjs/ha";

describe("Home Assistant login helpers", () => {
  test("normalizes URLs like Casita: https unless a scheme is given", () => {
    expect(normalizeHaUrl(" homeassistant.local:8123/ ")).toBe("https://homeassistant.local:8123");
    expect(normalizeHaUrl("http://192.168.1.20:8123//")).toBe("http://192.168.1.20:8123");
    expect(normalizeHaUrl("abc123.ui.nabu.casa")).toBe("https://abc123.ui.nabu.casa");
    expect(normalizeHaUrl("")).toBe("");
  });

  test("the settings page normalizes URLs the same way (its own copy)", () => {
    const html = readFileSync(new URL("../src/pkjs/config.html", import.meta.url), "utf8");
    const norm = html.match(/function normalizeUrl\(input\) \{[\s\S]*?\n  \}/);
    expect(norm).not.toBeNull();
    const pageNormalize = new Function(norm![0] + "; return normalizeUrl;")();
    for (const u of ["homeassistant.local:8123/", "http://192.168.1.20", "x.ui.nabu.casa", "https://a.b/"]) {
      expect(pageNormalize(u)).toBe(normalizeHaUrl(u));
    }
  });

  test("client id and redirect share a host (required by Home Assistant)", () => {
    expect(new URL(HA_CLIENT_ID).host).toBe(new URL(HA_REDIRECT_URI).host);
  });

  test("the GitHub Pages site (docs/) serves the client id page and the callback", () => {
    expect(HA_REDIRECT_URI.startsWith(HA_CLIENT_ID)).toBe(true);
    const callback = readFileSync(
      new URL("../docs/" + HA_REDIRECT_URI.slice(HA_CLIENT_ID.length), import.meta.url),
      "utf8",
    );
    expect(callback).toContain("pebblejs://close#");
    const index = readFileSync(new URL("../docs/index.html", import.meta.url), "utf8");
    expect(index).toContain('<link rel="redirect_uri" href="' + HA_REDIRECT_URI + '">');
  });

  test("login flow request bodies", () => {
    expect(JSON.parse(loginFlowStartBody("c", "r"))).toEqual({
      client_id: "c",
      handler: ["homeassistant", null],
      redirect_uri: "r",
    });
    expect(JSON.parse(loginFlowCredentialsBody("me", "pw", "c"))).toEqual({
      username: "me",
      password: "pw",
      client_id: "c",
    });
    expect(tokenExchangeBody("c", "id", "r")).toBe(
      "grant_type=authorization_code&code=c&client_id=id&redirect_uri=r",
    );
  });

  test("interprets login flow answers", () => {
    expect(interpretLoginFlow(200, { type: "form", flow_id: "f1", step_id: "init", errors: {} })).toEqual({
      kind: "step",
      flowId: "f1",
      stepId: "init",
    });
    expect(interpretLoginFlow(200, { type: "form", flow_id: "f1", step_id: "mfa", errors: null })).toEqual({
      kind: "step",
      flowId: "f1",
      stepId: "mfa",
    });
    expect(interpretLoginFlow(200, { type: "create_entry", result: "abc" })).toEqual({ kind: "code", code: "abc" });
    expect(
      interpretLoginFlow(200, { type: "form", flow_id: "f1", step_id: "init", errors: { base: "invalid_auth" } }),
    ).toEqual({ kind: "error", message: "wrong username or password" });
    expect(interpretLoginFlow(0, null).kind).toBe("error");
    expect(interpretLoginFlow(400, { message: "Invalid redirect URI" })).toEqual({
      kind: "error",
      message: "Home Assistant answered 400 (Invalid redirect URI)",
    });
    expect(interpretLoginFlow(200, { type: "abort", reason: "x" }).kind).toBe("error");
  });
});
