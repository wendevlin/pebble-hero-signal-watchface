import { describe, expect, test } from "bun:test";
import {
  Cond,
  defaultTempEntity,
  parseHaTemp,
  parseOpenMeteo,
  tempEntitiesFromStates,
  toTenthsC,
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

  test("Home Assistant weather entity and temperature sensor", () => {
    expect(
      parseHaTemp({
        entity_id: "weather.home",
        state: "partlycloudy",
        attributes: { temperature: 50, temperature_unit: "°F" },
      }),
    ).toBe(100);
    expect(
      parseHaTemp({
        entity_id: "sensor.living_room",
        state: "21.4",
        attributes: { unit_of_measurement: "°C", device_class: "temperature" },
      }),
    ).toBe(214);
    expect(parseHaTemp({ entity_id: "sensor.x", state: "unavailable", attributes: {} })).toBeNull();
    expect(parseHaTemp({ entity_id: "weather.x", state: "sunny", attributes: {} })).toBeNull();
    expect(parseHaTemp(null)).toBeNull();
  });

  test("entities: weather first, then temperature sensors, by name", () => {
    const states = [
      { entity_id: "sensor.humidity", attributes: { device_class: "humidity", friendly_name: "H" } },
      { entity_id: "sensor.kitchen", attributes: { device_class: "temperature", friendly_name: "Kitchen" } },
      { entity_id: "sensor.attic", attributes: { device_class: "temperature", friendly_name: "Attic" } },
      { entity_id: "weather.home", attributes: { friendly_name: "Home" } },
      { entity_id: "light.x", attributes: {} },
    ];
    const list = tempEntitiesFromStates(states);
    expect(list).toEqual([
      { entity_id: "weather.home", name: "Home", kind: "weather" },
      { entity_id: "sensor.attic", name: "Attic", kind: "sensor" },
      { entity_id: "sensor.kitchen", name: "Kitchen", kind: "sensor" },
    ]);
    expect(defaultTempEntity(list)).toBe("weather.home");
    expect(defaultTempEntity([])).toBe("");
    expect(tempEntitiesFromStates({})).toEqual([]);
  });
});
