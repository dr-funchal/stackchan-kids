import type { SettingsFile } from '../settings.ts';

// WMO weather codes (Open-Meteo) in Portuguese, with an emoji for the panel
const CODES: Record<number, [string, string]> = {
  0: ['céu limpo', '☀️'],
  1: ['quase sem nuvens', '🌤️'],
  2: ['parcialmente nublado', '⛅'],
  3: ['nublado', '☁️'],
  45: ['neblina', '🌫️'],
  48: ['neblina com geada', '🌫️'],
  51: ['garoa fraca', '🌦️'],
  53: ['garoa', '🌦️'],
  55: ['garoa forte', '🌧️'],
  56: ['garoa gelada', '🌧️'],
  57: ['garoa gelada forte', '🌧️'],
  61: ['chuva fraca', '🌦️'],
  63: ['chuva', '🌧️'],
  65: ['chuva forte', '🌧️'],
  66: ['chuva gelada', '🌧️'],
  67: ['chuva gelada forte', '🌧️'],
  71: ['neve fraca', '🌨️'],
  73: ['neve', '🌨️'],
  75: ['neve forte', '❄️'],
  77: ['grãos de neve', '🌨️'],
  80: ['pancadas de chuva fracas', '🌦️'],
  81: ['pancadas de chuva', '🌧️'],
  82: ['pancadas de chuva fortes', '⛈️'],
  85: ['pancadas de neve', '🌨️'],
  86: ['pancadas de neve fortes', '❄️'],
  95: ['trovoadas', '⛈️'],
  96: ['trovoadas com granizo', '⛈️'],
  99: ['trovoadas com granizo forte', '⛈️'],
};

export const describeCode = (code: number): { text: string; emoji: string } => {
  const [text, emoji] = CODES[code] ?? ['tempo indefinido', '🌡️'];
  return { text, emoji };
};

export interface Place {
  name: string;
  latitude: number;
  longitude: number;
  timezone: string;
}

export interface Forecast {
  place: string;
  current: { temperature: number; feelsLike: number; humidity: number; wind: number; text: string; emoji: string };
  days: { date: string; max: number; min: number; rainChance: number; text: string; emoji: string }[];
}

const CACHE_MS = 10 * 60_000;

export class WeatherService {
  #settings: SettingsFile;
  #fetch: typeof fetch;
  #cache = new Map<string, { at: number; data: Forecast }>();

  constructor(settings: SettingsFile, fetchImpl: typeof fetch = fetch) {
    this.#settings = settings;
    this.#fetch = fetchImpl;
  }

  async geocode(city: string): Promise<Place> {
    const url = new URL('https://geocoding-api.open-meteo.com/v1/search');
    url.search = new URLSearchParams({ name: city, count: '1', language: 'pt', format: 'json' }).toString();
    const res = await this.#fetch(url, { signal: AbortSignal.timeout(8000) });
    const data = (await res.json()) as {
      results?: { name: string; admin1?: string; country?: string; latitude: number; longitude: number; timezone: string }[];
    };
    const r = data.results?.[0];
    if (!r) throw new Error(`city not found: ${city}`);
    return {
      name: [r.name, r.admin1, r.country].filter(Boolean).join(', '),
      latitude: r.latitude,
      longitude: r.longitude,
      timezone: r.timezone,
    };
  }

  async setHome(city: string): Promise<Place | undefined> {
    const place = city.trim() ? await this.geocode(city.trim()) : undefined;
    await this.#settings.update((s) => {
      s.homeCity = city.trim();
      if (place) s.home = place;
      else delete s.home;
    });
    this.#cache.clear();
    return place;
  }

  async forecast(city?: string): Promise<Forecast> {
    const place = city?.trim() ? await this.geocode(city) : this.#settings.get().home;
    if (!place) throw new Error('no home city configured');
    const key = `${place.latitude},${place.longitude}`;
    const cached = this.#cache.get(key);
    if (cached && Date.now() - cached.at < CACHE_MS) return cached.data;

    const url = new URL('https://api.open-meteo.com/v1/forecast');
    url.search = new URLSearchParams({
      latitude: String(place.latitude),
      longitude: String(place.longitude),
      current: 'temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m',
      daily: 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max',
      timezone: place.timezone || 'auto',
      forecast_days: '4',
    }).toString();
    const res = await this.#fetch(url, { signal: AbortSignal.timeout(8000) });
    if (!res.ok) throw new Error(`weather service error ${res.status}`);
    const d = (await res.json()) as {
      current: Record<string, number>;
      daily: { time: string[] } & Record<string, number[]>;
    };
    const data: Forecast = {
      place: place.name,
      current: {
        temperature: Math.round(d.current.temperature_2m!),
        feelsLike: Math.round(d.current.apparent_temperature!),
        humidity: Math.round(d.current.relative_humidity_2m!),
        wind: Math.round(d.current.wind_speed_10m!),
        ...describeCode(d.current.weather_code!),
      },
      days: d.daily.time.map((date, i) => ({
        date,
        max: Math.round(d.daily.temperature_2m_max![i]!),
        min: Math.round(d.daily.temperature_2m_min![i]!),
        rainChance: Math.round(d.daily.precipitation_probability_max![i] ?? 0),
        ...describeCode(d.daily.weather_code![i]!),
      })),
    };
    this.#cache.set(key, { at: Date.now(), data });
    return data;
  }
}
