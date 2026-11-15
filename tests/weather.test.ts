import { describe, expect, test } from "bun:test";
import {
  Cond,
  haConditionToCond,
  haIsDay,
  parseHaWeather,
  parseOpenMeteo,
  toTenthsC,
  weatherEntitiesFromStates,
  wmoToCond,
} from "../src/pkjs/weather";

describe("wmoToCond", () => {
  test.each([
    [0, Cond.CLEAR],
    [2, Cond.PARTLY],
    [3, Cond.CLOUDY],
    [45, Cond.FOG],
    [53, Cond.RAIN],
    [57, Cond.SLEET],
    [61, Cond.RAIN],
    [65, Cond.POURING],
    [67, Cond.SLEET],
    [73, Cond.SNOW],
    [82, Cond.POURING],
    [86, Cond.SNOW],
    [95, Cond.THUNDER],
    [99, Cond.HAIL],
    [42, Cond.UNKNOWN],
  ])("WMO %d", (code, cond) => {
    expect(wmoToCond(code)).toBe(cond);
  });
});

describe("Home Assistant conditions", () => {
  test("maps every documented weather state", () => {
    const states = [
      "clear-night", "cloudy", "fog", "hail", "lightning", "lightning-rainy", "partlycloudy",
      "pouring", "rainy", "snowy", "snowy-rainy", "sunny", "windy", "windy-variant",
    ];
    for (const s of states) expect(haConditionToCond(s)).not.toBe(Cond.UNKNOWN);
  });

  test("unknown and missing states", () => {
    expect(haConditionToCond("exceptional")).toBe(Cond.UNKNOWN);
    expect(haConditionToCond(null)).toBe(Cond.UNKNOWN);
    expect(haConditionToCond("unavailable")).toBe(Cond.UNKNOWN);
  });

  test("day/night prefers sun.sun, then clear-night, then the clock", () => {
    expect(haIsDay("below_horizon", "sunny", 12)).toBe(false);
    expect(haIsDay("above_horizon", "clear-night", 2)).toBe(true);
    expect(haIsDay(null, "clear-night", 12)).toBe(false);
    expect(haIsDay(null, "rainy", 12)).toBe(true);
    expect(haIsDay(null, "rainy", 23)).toBe(false);
  });
});

describe("toTenthsC", () => {
  test("celsius and fahrenheit", () => {
    expect(toTenthsC(21.46, "°C")).toBe(215);
    expect(toTenthsC("-3.2", "°C")).toBe(-32);
    expect(toTenthsC(212, "°F")).toBe(1000);
    expect(toTenthsC("unavailable", "°C")).toBeNull();
  });
});

describe("parsers", () => {
  test("Open-Meteo current block", () => {
    expect(
      parseOpenMeteo({ current: { temperature_2m: 14.04, weather_code: 61, is_day: 0 } }),
    ).toEqual({ tenthsC: 140, cond: Cond.RAIN, isDay: false });
    expect(parseOpenMeteo({})).toBeNull();
    expect(parseOpenMeteo(null)).toBeNull();
  });

  test("Home Assistant weather entity", () => {
    const entity = {
      entity_id: "weather.home",
      state: "partlycloudy",
      attributes: { temperature: 50, temperature_unit: "°F" },
    };
    expect(parseHaWeather(entity, "above_horizon", 9)).toEqual({
      tenthsC: 100,
      cond: Cond.PARTLY,
      isDay: true,
    });
    expect(parseHaWeather({ state: "sunny", attributes: {} }, null, 9)).toBeNull();
  });

  test("weather entities from /api/states", () => {
    const states = [
      { entity_id: "sensor.x", attributes: { friendly_name: "X" } },
      { entity_id: "weather.home", attributes: { friendly_name: "Home" } },
      { entity_id: "weather.forecast_attic" },
    ];
    expect(weatherEntitiesFromStates(states)).toEqual([
      { entity_id: "weather.home", name: "Home" },
      { entity_id: "weather.forecast_attic", name: "weather.forecast_attic" },
    ]);
    expect(weatherEntitiesFromStates({})).toEqual([]);
  });
});
