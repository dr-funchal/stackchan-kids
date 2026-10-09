import { JsonFile } from './store.ts';

export interface ExternalTool {
  name: string;
  description: string;
}

export interface ExternalServer {
  id: string;
  name: string;
  url: string;
  enabled: boolean;
  /** Header names only; the values live encrypted in the secret store. */
  headerNames: string[];
  /** Tools the parent switched on. External tools start off: we cannot know what they do. */
  enabledTools: string[];
  discovered: ExternalTool[];
  lastError?: string;
  lastCheckedAt?: string;
}

export interface Settings {
  /** Module name -> on/off. Missing = on. */
  modules: Record<string, boolean>;
  disabledTools: string[];
  homeCity: string;
  home?: { name: string; latitude: number; longitude: number; timezone: string };
  external: ExternalServer[];
}

export const defaultSettings = (): Settings => ({
  modules: {},
  disabledTools: [],
  homeCity: '',
  external: [],
});

export type SettingsFile = JsonFile<Settings>;
