import { describe, expect, test } from "bun:test";
import { readFileSync } from "node:fs";
import {
  HA_CLIENT_ID,
  HA_REDIRECT_URI,
  base64UrlDecode,
  base64UrlEncode,
  buildAuthorizeUrl,
  decodeState,
  encodeState,
  normalizeHaUrl,
  tokenExchangeBody,
} from "../src/pkjs/ha";

describe("Home Assistant OAuth helpers", () => {
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

  test("base64url round-trips UTF-8", () => {
    for (const s of ["", "a", "ab", "abc", "Grüß Gott 🦸"]) {
      expect(base64UrlDecode(base64UrlEncode(s))).toBe(s);
    }
    expect(base64UrlEncode("??>")).not.toMatch(/[+/=]/);
  });

  test("state carries the HA URL (same {u, n} format as Casita)", () => {
    const state = encodeState("https://ha.example", "123");
    expect(JSON.parse(base64UrlDecode(state))).toEqual({ u: "https://ha.example", n: "123" });
    expect(decodeState(state)).toEqual({ haUrl: "https://ha.example", nonce: "123", settings: null });
  });

  test("the settings page encodes the state the same way (its own copy)", () => {
    const html = readFileSync(new URL("../src/pkjs/config.html", import.meta.url), "utf8");
    const b64 = html.match(/var B64 = '[^']+';/);
    const fn = html.match(/function base64UrlEncode\(str\) \{[\s\S]*?\n  \}/);
    expect(b64).not.toBeNull();
    expect(fn).not.toBeNull();
    const pageEncode = new Function(b64![0] + fn![0] + "; return base64UrlEncode;")();
    const payload = JSON.stringify({ u: "https://ha.example", n: "1-2" });
    expect(pageEncode(payload)).toBe(base64UrlEncode(payload));
    expect(decodeState(pageEncode(payload)).haUrl).toBe("https://ha.example");
  });

  test("client id and redirect share a host (required by Home Assistant)", () => {
    expect(new URL(HA_CLIENT_ID).host).toBe(new URL(HA_REDIRECT_URI).host);
  });

  test("the GitHub Pages site (docs/) serves the client id page and the callback", () => {
    // docs/ is published at HA_CLIENT_ID, so the callback's path below it must exist.
    expect(HA_REDIRECT_URI.startsWith(HA_CLIENT_ID)).toBe(true);
    const callbackPath = HA_REDIRECT_URI.slice(HA_CLIENT_ID.length);
    const callback = readFileSync(new URL("../docs/" + callbackPath, import.meta.url), "utf8");
    expect(callback).toContain("pebblejs://close#");
    const index = readFileSync(new URL("../docs/index.html", import.meta.url), "utf8");
    expect(index).toContain('<link rel="redirect_uri" href="' + HA_REDIRECT_URI + '">');
  });

  test("authorize URL and token body", () => {
    const url = buildAuthorizeUrl("https://ha.example", HA_CLIENT_ID, HA_REDIRECT_URI, "s");
    expect(url).toStartWith("https://ha.example/auth/authorize?client_id=https%3A%2F%2F");
    expect(url).toContain("&response_type=code&state=s");
    expect(tokenExchangeBody("c", "id", "r")).toBe(
      "grant_type=authorization_code&code=c&client_id=id&redirect_uri=r",
    );
  });
});
