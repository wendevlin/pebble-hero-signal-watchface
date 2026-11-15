/*
 * Pure weather helpers (phone-side, unit-tested in tests/weather.test.ts).
 *
 * The outdoor weather comes from the phone location (Open-Meteo) and is mapped
 * onto the watch's weather scenes here; keep `Cond` in sync with `WeatherCond`
 * in src/c/scene.h. Home Assistant adds a second temperature (a weather entity
 * or a temperature sensor), shown on the watch as "12°/21°".
 */

export const Cond = {
  UNKNOWN: 0,
  CLEAR: 1,
  PARTLY: 2,
  CLOUDY: 3,
  FOG: 4,
  RAIN: 5,
  POURING: 6,
  SNOW: 7,
  SLEET: 8,
  THUNDER: 9,
  HAIL: 10,
  WIND: 11,
} as const;

export type CondCode = (typeof Cond)[keyof typeof Cond];

/** Open-Meteo / WMO weather interpretation code -> watch condition. */
export function wmoToCond(code: number): CondCode {
  if (code === 0) return Cond.CLEAR;
  if (code === 1 || code === 2) return Cond.PARTLY;
  if (code === 3) return Cond.CLOUDY;
  if (code === 45 || code === 48) return Cond.FOG;
  if (code >= 51 && code <= 57) return code >= 56 ? Cond.SLEET : Cond.RAIN; // drizzle
  if (code === 61 || code === 63 || code === 80 || code === 81) return Cond.RAIN;
  if (code === 65 || code === 82) return Cond.POURING;
  if (code === 66 || code === 67) return Cond.SLEET; // freezing rain
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return Cond.SNOW;
  if (code === 95) return Cond.THUNDER;
  if (code === 96 || code === 99) return Cond.HAIL; // thunderstorm with hail
  return Cond.UNKNOWN;
}

/**
 * Temperature in `unit` (anything containing "F" is Fahrenheit) to tenths of a
 * degree Celsius, the format the watch stores. Null for non-numbers.
 */
export function toTenthsC(value: unknown, unit?: string | null): number | null {
  const n = typeof value === "number" ? value : parseFloat(String(value));
  if (!isFinite(n)) return null;
  const celsius = unit && /f/i.test(unit) ? ((n - 32) * 5) / 9 : n;
  return Math.round(celsius * 10);
}

export interface WeatherReading {
  tenthsC: number;
  cond: CondCode;
  isDay: boolean;
}

/** Parse an Open-Meteo `current=temperature_2m,weather_code,is_day` response. */
export function parseOpenMeteo(body: unknown): WeatherReading | null {
  // No optional chaining / nullish coalescing in this file: the phone's JS
  // runtime is not guaranteed to support them, and bun does not down-level.
  const obj = body as { current?: Record<string, unknown> } | null;
  const current = obj ? obj.current : undefined;
  if (!current) return null;
  const tenthsC = toTenthsC(current.temperature_2m, "°C");
  if (tenthsC === null) return null;
  const code = Number(current.weather_code);
  return {
    tenthsC,
    cond: isFinite(code) ? wmoToCond(code) : Cond.UNKNOWN,
    isDay: Number(current.is_day) !== 0,
  };
}

export interface HaStateObject {
  entity_id?: string;
  state?: string;
  attributes?: {
    temperature?: unknown;
    temperature_unit?: string;
    unit_of_measurement?: string;
    device_class?: string;
    friendly_name?: string;
  };
}

/** A Home Assistant entity that can provide the second temperature. */
export interface TempEntity {
  entity_id: string;
  name: string;
  kind: "weather" | "sensor";
}

/** `weather.*` entities and temperature sensors from GET /api/states. */
export function tempEntitiesFromStates(states: unknown): TempEntity[] {
  if (!Array.isArray(states)) return [];
  const out: TempEntity[] = [];
  for (const s of states as HaStateObject[]) {
    const id = s && typeof s.entity_id === "string" ? s.entity_id : "";
    const attrs = (s && s.attributes) || {};
    let kind: "weather" | "sensor" | null = null;
    if (id.indexOf("weather.") === 0) kind = "weather";
    else if (id.indexOf("sensor.") === 0 && attrs.device_class === "temperature") kind = "sensor";
    if (!kind) continue;
    out.push({ entity_id: id, name: attrs.friendly_name || id, kind: kind });
  }
  out.sort(function (a, b) {
    if (a.kind !== b.kind) return a.kind === "weather" ? -1 : 1;
    return a.name < b.name ? -1 : a.name > b.name ? 1 : 0;
  });
  return out;
}

/** The entity to pre-select after logging in: a weather entity if there is one. */
export function defaultTempEntity(list: TempEntity[]): string {
  return list.length > 0 ? list[0].entity_id : "";
}

/**
 * Temperature of a weather entity (attributes.temperature) or a temperature
 * sensor (its state), in tenths of a degree Celsius.
 */
export function parseHaTemp(entity: HaStateObject | null): number | null {
  if (!entity) return null;
  const attrs = entity.attributes || {};
  const id = entity.entity_id || "";
  if (id.indexOf("weather.") === 0) {
    return toTenthsC(attrs.temperature, attrs.temperature_unit);
  }
  return toTenthsC(entity.state, attrs.unit_of_measurement);
}
