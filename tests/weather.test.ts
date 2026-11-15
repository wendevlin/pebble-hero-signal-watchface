import { describe, expect, test } from "bun:test";
import {
  Cond,
  defaultTempEntity,
  parseTempEntityTemplate,
  tempEntityTemplate,
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
    expect(list.map((e) => [e.entity_id, e.name, e.kind])).toEqual([
      ["weather.home", "Home", "weather"],
      ["sensor.attic", "Attic", "sensor"],
      ["sensor.kitchen", "Kitchen", "sensor"],
    ]);
    expect(defaultTempEntity(list)).toBe("weather.home");
    expect(defaultTempEntity([])).toBe("");
    expect(tempEntitiesFromStates({})).toEqual([]);
  });

  test("entity template: floors only when asked for", () => {
    expect(tempEntityTemplate(true)).toContain("floor_name(s.entity_id)");
    expect(tempEntityTemplate(true)).toContain("floors() | map('floor_name')");
    expect(tempEntityTemplate(false)).not.toContain("floor_name");
  });

  test("parses the rendered entity template", () => {
    const rendered = JSON.stringify({
      floors: ["Ground floor", "First floor"],
      items: [
        { e: "sensor.kitchen", n: "Kitchen", k: "sensor", v: "23.1", u: "°C", a: "Kitchen", f: "Ground floor", d: null },
        { e: "weather.home", n: "Forecast Home", k: "weather", v: 14.2, u: "°C", a: null, f: null, d: "Met.no" },
      ],
    });
    expect(parseTempEntityTemplate(rendered)).toEqual({
      floors: ["Ground floor", "First floor"],
      entities: [
        { entity_id: "weather.home", name: "Forecast Home", kind: "weather", state: "14.2", unit: "°C", floor: "", area: "", device: "Met.no" },
        { entity_id: "sensor.kitchen", name: "Kitchen", kind: "sensor", state: "23.1", unit: "°C", floor: "Ground floor", area: "Kitchen", device: "" },
      ],
    });
    expect(parseTempEntityTemplate("not json")).toBeNull();
    expect(parseTempEntityTemplate("{}")).toBeNull();
  });
});
