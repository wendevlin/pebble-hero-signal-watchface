/*
 * Pure weather helpers (phone-side, unit-tested in tests/weather.test.ts).
 *
 * The watch draws a small set of weather scenes; both weather sources are
 * mapped onto that set here. Keep `Cond` in sync with `WeatherCond` in
 * src/c/scene.h.
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

const HA_CONDITIONS: { [state: string]: CondCode } = {
  "clear-night": Cond.CLEAR,
  sunny: Cond.CLEAR,
  partlycloudy: Cond.PARTLY,
  cloudy: Cond.CLOUDY,
  fog: Cond.FOG,
  rainy: Cond.RAIN,
  pouring: Cond.POURING,
  snowy: Cond.SNOW,
  "snowy-rainy": Cond.SLEET,
  lightning: Cond.THUNDER,
  "lightning-rainy": Cond.THUNDER,
  hail: Cond.HAIL,
  windy: Cond.WIND,
  "windy-variant": Cond.WIND,
};

/** Home Assistant `weather.*` entity state -> watch condition. */
export function haConditionToCond(state: string | null | undefined): CondCode {
  if (!state) return Cond.UNKNOWN;
  const c = HA_CONDITIONS[state];
  return c === undefined ? Cond.UNKNOWN : c;
}

/**
 * Day/night for a Home Assistant reading: the `sun.sun` entity when we have
 * it, otherwise the condition itself ("clear-night"), otherwise the clock.
 */
export function haIsDay(sunState: string | null, condition: string | null, hour: number): boolean {
  if (sunState === "above_horizon") return true;
  if (sunState === "below_horizon") return false;
  if (condition === "clear-night") return false;
  return hour >= 7 && hour < 19;
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
    friendly_name?: string;
  };
}

/** Parse a `weather.*` state object (+ optional `sun.sun` state). */
export function parseHaWeather(
  entity: HaStateObject | null,
  sunState: string | null,
  hour: number,
): WeatherReading | null {
  if (!entity || !entity.attributes) return null;
  const tenthsC = toTenthsC(entity.attributes.temperature, entity.attributes.temperature_unit);
  if (tenthsC === null) return null;
  const state = entity.state === undefined ? null : entity.state;
  return {
    tenthsC,
    cond: haConditionToCond(state),
    isDay: haIsDay(sunState, state, hour),
  };
}

export interface WeatherEntity {
  entity_id: string;
  name: string;
}

/** All `weather.*` entities from GET /api/states, sorted by name. */
export function weatherEntitiesFromStates(states: unknown): WeatherEntity[] {
  if (!Array.isArray(states)) return [];
  const out: WeatherEntity[] = [];
  for (const s of states as HaStateObject[]) {
    const id = s && typeof s.entity_id === "string" ? s.entity_id : "";
    if (id.indexOf("weather.") !== 0) continue;
    out.push({ entity_id: id, name: (s.attributes && s.attributes.friendly_name) || id });
  }
  out.sort(function (a, b) {
    return a.name < b.name ? -1 : a.name > b.name ? 1 : 0;
  });
  return out;
}
