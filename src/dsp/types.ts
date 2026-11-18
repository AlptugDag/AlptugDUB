export type VoiceType =
  | 'Unknown'
  | 'Bass'
  | 'Baritone'
  | 'Tenor'
  | 'Alto'
  | 'MezzoSoprano'
  | 'Soprano'
  | 'Child';

export type ScaleMode = 'auto' | 'major' | 'minor' | 'chromatic' | 'hijaz' | 'pentatonic';

export type DetectedScale = 'major' | 'minor' | 'chromatic';

export interface VoiceSummary {
  medianF0: number;
  lowF0: number;
  highF0: number;
  formants: [number, number, number]; // F1, F2, F3 in Hz
  brightness: number; // Spectral centroid in Hz
  breathiness: number; // 0..1
  seconds: number;
  type: VoiceType;
  ready: boolean;
}

export interface CharacterPreset {
  id: string;
  index: number;
  nameTr: string;
  nameEn: string;
  targetF0: number; // 0 = relative
  pitch: number; // semitones
  formant: number; // semitones
  age: number; // -50..+50
  nasal: number; // 0..1
  body: number; // dB
  air: number; // dB
  regen: number; // 0..1
  glottal: number; // -1..+1
  growl: number; // 0..1
  breath: number; // 0..1
}

export interface StudioParams {
  character: number;
  pitch: number; // -18..+18 st
  formant: number; // -8..+8 st
  follow: boolean; // Link formant to pitch (0.45 ratio, mathematically synced)
  regen: number; // 0..1
  glottal: number; // -1..+1
  growl: number; // 0..1
  breath: number; // 0..1
  autotune: number; // 0..1
  retuneSpeedMs: number; // 5..120 ms (new in v3.0)
  vibratoPreserve: number; // 0..1 (new in v3.0)
  scaleMode: ScaleMode; // auto or locked scale (new in v3.0)
  manualKey: number; // 0=C .. 11=B (new in v3.0)
  age: number; // -50..+50
  nasal: number; // 0..1
  body: number; // -6..+6 dB
  presence: number; // -6..+6 dB
  air: number; // -6..+6 dB
  mix: number; // 0..1
  output: number; // -12..+12 dB
  // Pre-conditioning & mastering controls (new in v3.0)
  hpfHz: number; // 40..160 Hz input rumble filter
  gateDb: number; // -70..-24 dBFS noise gate floor
  deEsser: number; // 0..1 dynamic sibilance control
  // Engine switches
  aiPitch: boolean;
  consonantGuard: boolean;
  autoMatch: boolean;
  loudness: boolean;
  autoTract: boolean;
  autoResponse: boolean;
  autoSteady: boolean;
  autoTone: boolean;
  bypass: boolean;
}

export interface AudioTelemetry {
  inputLevel: number; // peak linear 0..1
  outputLevel: number; // peak linear 0..1
  inputDb: number; // dBFS
  outputDb: number; // dBFS
  limiterGrDb: number; // negative dB (0 = no GR)
  lufsDeltaDb: number; // loudness compensation applied (dB)
  liveF0: number; // Hz, 0 if unvoiced
  targetF0: number; // shifted + tuned Hz
  confidence: number; // 0..1
  voiced: number; // 0..1
  consonant: number; // 0..1
  regenActive: number; // 0..1
  detectedKey: number; // 0..11
  detectedScale: DetectedScale;
  tuneCents: number; // active Auto-Tune correction in cents
  waveHistory: number[]; // 64-slot amplitude/signed wave envelope
  drySpectrum: number[]; // 64 log-spaced dB bins (-80..0)
  wetSpectrum: number[]; // 64 log-spaced dB bins (-80..0)
  pitchTrace: Array<{ inputHz: number; outputHz: number; voiced: boolean }>;
  summary: VoiceSummary;
}

export interface DiagnosticCheckResult {
  id: string;
  titleTr: string;
  titleEn: string;
  defectFixedTr: string;
  defectFixedEn: string;
  measured: string;
  target: string;
  passed: boolean;
}

export const DEFAULT_STUDIO_PARAMS: StudioParams = {
  character: 1, // İri adam (matches AlptugDUB reference showcase)
  pitch: -4.5,
  formant: -2.8,
  follow: false,
  regen: 0.92,
  glottal: -0.55,
  growl: 0.18,
  breath: 0.05,
  autotune: 0.0,
  retuneSpeedMs: 22,
  vibratoPreserve: 0.8,
  scaleMode: 'auto',
  manualKey: 0,
  age: 3,
  nasal: 0.0,
  body: 2.0,
  presence: 0.5,
  air: 0.5,
  mix: 1.0,
  output: 0.0,
  hpfHz: 75,
  gateDb: -54,
  deEsser: 0.25,
  aiPitch: true,
  consonantGuard: true,
  autoMatch: true,
  loudness: true,
  autoTract: true,
  autoResponse: true,
  autoSteady: false,
  autoTone: true,
  bypass: false,
};
