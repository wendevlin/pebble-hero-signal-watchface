import { describe, expect, test } from "bun:test";
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
  test("normalizes URLs", () => {
    expect(normalizeHaUrl(" homeassistant.local:8123/ ")).toBe("https://homeassistant.local:8123");
    expect(normalizeHaUrl("http://10.0.0.2:8123//")).toBe("http://10.0.0.2:8123");
    expect(normalizeHaUrl("")).toBe("");
  });

  test("base64url round-trips UTF-8", () => {
    for (const s of ["", "a", "ab", "abc", "Grüß Gott 🦸"]) {
      expect(base64UrlDecode(base64UrlEncode(s))).toBe(s);
    }
    expect(base64UrlEncode("??>")).not.toMatch(/[+/=]/);
  });

  test("state carries the HA URL", () => {
    const state = encodeState("https://ha.example", "123");
    expect(decodeState(state)).toEqual({ haUrl: "https://ha.example", nonce: "123" });
  });

  test("client id and redirect share a host (required by Home Assistant)", () => {
    expect(new URL(HA_CLIENT_ID).host).toBe(new URL(HA_REDIRECT_URI).host);
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
