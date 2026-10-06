import type { GatewayModule } from '../registry/registry.ts';
import type { WeatherService } from '../services/weather.ts';

export function weatherModule(weather: WeatherService): GatewayModule {
  return {
    name: 'weather',
    tools: () => [
      {
        name: 'weather_forecast',
        description:
          'Current weather and the forecast for the next days. Without a city it uses the family\'s home city. Use for ' +
          '"vai chover?", "está frio lá fora?", "como vai estar o tempo amanhã?". Answer simply, for children.',
        inputSchema: {
          type: 'object',
          properties: { city: { type: 'string', maxLength: 80, description: 'Optional city; default is home.' } },
          additionalProperties: false,
        },
        timeoutMs: 12_000,
        summarize: (args) => (args.city ? `Clima em ${args.city}` : 'Clima de casa'),
        async handler(args) {
          let f;
          try {
            f = await weather.forecast(args.city ? String(args.city) : undefined);
          } catch (err) {
            if (String(err).includes('no home city')) {
              return 'The home city is not configured yet. Ask which city, or tell a grown-up to set it in the panel.';
            }
            throw err;
          }
          const c = f.current;
          const days = f.days
            .map((d, i) => `${['today', 'tomorrow'][i] ?? d.date}: ${d.text}, ${d.min}-${d.max}°C, rain chance ${d.rainChance}%`)
            .join('; ');
          return `Weather in ${f.place}: now ${c.temperature}°C (feels like ${c.feelsLike}°C), ${c.text}, ` +
            `humidity ${c.humidity}%, wind ${c.wind} km/h. Forecast: ${days}.`;
        },
      },
    ],
  };
}
