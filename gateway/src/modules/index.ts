import type { GatewayModule } from '../registry/registry.ts';
import type { MusicLibrary } from '../services/music-library.ts';
import type { SpotifyService } from '../services/spotify.ts';
import type { WeatherService } from '../services/weather.ts';
import type { SettingsFile } from '../settings.ts';
import { diagnosticsModule } from './diagnostics.ts';
import { musicModule } from './music.ts';
import { spotifyModule } from './spotify.ts';
import { storiesModule } from './stories.ts';
import { weatherModule } from './weather.ts';

export interface ModuleContext {
  dataDir: string;
  timezone: string;
  settings: SettingsFile;
  music: MusicLibrary;
  spotify: SpotifyService;
  weather: WeatherService;
}

export interface ModuleInfo {
  name: string;
  title: string;
  description: string;
  build(ctx: ModuleContext): GatewayModule;
}

/**
 * Module catalog. To add a service: create src/modules/<name>.ts returning a GatewayModule and add one entry here.
 * The panel lists it, turns it on and off, and the robot sees its tools; nothing else changes.
 */
export const MODULES: ModuleInfo[] = [
  {
    name: 'diagnostics',
    title: 'Diagnóstico',
    description: 'Palavra secreta, para testar o caminho robô → gateway. Deixe desligado no dia a dia.',
    build: (ctx) => diagnosticsModule({ timezone: ctx.timezone }),
  },
  {
    name: 'stories',
    title: 'Histórias',
    description: 'Biblioteca online de histórias, lidas página por página.',
    build: (ctx) => storiesModule({ dataDir: ctx.dataDir }),
  },
  {
    name: 'music',
    title: 'Música',
    description: '"Toca X": no alto-falante do robô (biblioteca própria) ou no Spotify.',
    build: (ctx) => musicModule({ library: ctx.music, spotify: ctx.spotify, settings: ctx.settings }),
  },
  {
    name: 'spotify',
    title: 'Controle do Spotify',
    description: 'Uma ferramenta para pausar, pular, volume, aparelhos e "o que está tocando".',
    build: (ctx) => spotifyModule(ctx.spotify),
  },
  {
    name: 'weather',
    title: 'Clima',
    description: 'Tempo agora e previsão para a cidade de casa.',
    build: (ctx) => weatherModule(ctx.weather),
  },
];
