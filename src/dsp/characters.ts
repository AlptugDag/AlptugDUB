import type { CharacterPreset, DetectedScale, ScaleMode, StudioParams, VoiceSummary, VoiceType } from './types';

export const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'] as const;

export const CHARACTER_PRESETS: readonly CharacterPreset[] = [
  { id: 'natural', index: 0, nameTr: 'Doğal', nameEn: 'Natural', targetF0: 0, pitch: 0.0, formant: 0.0, age: 0, nasal: 0.0, body: 0.0, air: 0.0, regen: 0.0, glottal: 0.0, growl: 0.0, breath: 0.0 },
  { id: 'bigman', index: 1, nameTr: 'İri adam', nameEn: 'Big man', targetF0: 96, pitch: -4.5, formant: -2.8, age: 3, nasal: 0.0, body: 2.0, air: 0.5, regen: 0.92, glottal: -0.55, growl: 0.18, breath: 0.05 },
  { id: 'giant', index: 2, nameTr: 'Dev', nameEn: 'Giant', targetF0: 76, pitch: -7.5, formant: -4.5, age: 6, nasal: 0.0, body: 3.0, air: 2.0, regen: 0.96, glottal: -0.8, growl: 0.38, breath: 0.08 },
  { id: 'monster', index: 3, nameTr: 'Canavar', nameEn: 'Monster', targetF0: 66, pitch: -9.5, formant: -4.2, age: 9, nasal: 0.0, body: 3.0, air: 2.5, regen: 1.0, glottal: -0.9, growl: 0.68, breath: 0.16 },
  { id: 'man', index: 4, nameTr: 'Erkek', nameEn: 'Man', targetF0: 115, pitch: -3.4, formant: -0.8, age: 0, nasal: 0.0, body: 1.0, air: 0.5, regen: 0.9, glottal: -0.3, growl: 0.06, breath: 0.04 },
  { id: 'woman', index: 5, nameTr: 'Kadın', nameEn: 'Woman', targetF0: 215, pitch: 5.6, formant: 2.6, age: 0, nasal: 0.0, body: 0.0, air: 0.5, regen: 0.95, glottal: 0.55, growl: 0.0, breath: 0.14 },
  { id: 'girl', index: 6, nameTr: 'Küçük kız', nameEn: 'Little girl', targetF0: 295, pitch: 8.0, formant: 4.6, age: -40, nasal: 0.03, body: -2.0, air: 0.5, regen: 0.96, glottal: 0.75, growl: 0.0, breath: 0.18 },
  { id: 'boy', index: 7, nameTr: 'Küçük oğlan', nameEn: 'Little boy', targetF0: 255, pitch: 6.5, formant: 3.8, age: -35, nasal: 0.0, body: -1.5, air: 0.5, regen: 0.94, glottal: 0.45, growl: 0.0, breath: 0.12 },
  { id: 'oldman', index: 8, nameTr: 'Yaşlı adam', nameEn: 'Old man', targetF0: 124, pitch: -2.2, formant: -0.6, age: 18, nasal: 0.05, body: 0.0, air: -1.0, regen: 0.92, glottal: -0.25, growl: 0.28, breath: 0.18 },
  { id: 'oldwoman', index: 9, nameTr: 'Yaşlı kadın', nameEn: 'Old woman', targetF0: 195, pitch: 3.8, formant: 1.8, age: 18, nasal: 0.07, body: 0.0, air: -1.0, regen: 0.94, glottal: 0.3, growl: 0.18, breath: 0.22 },
  { id: 'witch', index: 10, nameTr: 'Cadı', nameEn: 'Witch', targetF0: 235, pitch: 5.0, formant: 2.4, age: 15, nasal: 0.3, body: -2.0, air: 0.5, regen: 0.96, glottal: -0.45, growl: 0.42, breath: 0.2 },
  { id: 'dwarf', index: 11, nameTr: 'Cüce', nameEn: 'Dwarf', targetF0: 120, pitch: -2.4, formant: 2.2, age: 3, nasal: 0.1, body: 1.0, air: 0.0, regen: 0.92, glottal: -0.65, growl: 0.3, breath: 0.06 },
  { id: 'goblin', index: 12, nameTr: 'Goblin', nameEn: 'Goblin', targetF0: 265, pitch: 6.0, formant: 3.4, age: 3, nasal: 0.28, body: -2.0, air: 0.0, regen: 0.96, glottal: -0.5, growl: 0.48, breath: 0.14 },
];

export const LINK_RATIO = 0.45;

export function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

export function ageShape(age: number): { pitch: number; formant: number; air: number } {
  const a = clamp(age / 50, -1, 1);
  if (a < 0) {
    return { pitch: -1.0 * a, formant: -1.2 * a, air: -1.5 * a };
  }
  return { pitch: 0, formant: -0.3 * a, air: -0.5 * a };
}

export function estimateSpeakerTractSemitones(summary: VoiceSummary): number {
  const [f1, f2, f3] = summary.formants;
  if (f1 > 0 && f2 > 0 && f3 > 0) {
    const fMean = (f1 + f2 + f3) / 3;
    const fmtTract = 12 * Math.log2(clamp(fMean / 1380, 0.78, 1.45)) * 0.65;
    const f0Prior =
      summary.medianF0 > 130
        ? clamp((2.2 * Math.log2(summary.medianF0 / 130)) / Math.log2(225 / 130), 0, 3.8)
        : 0;
    return clamp(0.85 * fmtTract + 0.15 * f0Prior, -1.8, 4.2);
  }
  if (summary.medianF0 <= 125) return 0;
  return clamp((2.2 * Math.log2(summary.medianF0 / 125)) / Math.log2(225 / 125), 0, 4.0);
}

export function classifyVoice(f0: number, formants: [number, number, number] = [0, 0, 0]): VoiceType {
  if (f0 <= 0) return 'Unknown';
  const fMean =
    formants[0] > 0 && formants[1] > 0 && formants[2] > 0
      ? (formants[0] + formants[1] + formants[2]) / 3
      : 0;
  if (fMean > 100 && fMean < 1600) {
    if (f0 < 102) return 'Bass';
    if (f0 < 126) return 'Baritone';
    if (f0 < 168) return 'Tenor';
    return 'Alto';
  }
  if (fMean >= 1600) {
    if (f0 < 102) return 'Bass';
    if (f0 < 126) return 'Baritone';
    if (f0 < 152) return 'Tenor';
    if (f0 < 198) return 'Alto';
    if (f0 < 228) return 'MezzoSoprano';
    if (f0 < 265) return 'Soprano';
    return 'Child';
  }
  if (f0 < 102) return 'Bass';
  if (f0 < 126) return 'Baritone';
  if (f0 < 162) return 'Tenor';
  if (f0 < 198) return 'Alto';
  if (f0 < 228) return 'MezzoSoprano';
  if (f0 < 265) return 'Soprano';
  return 'Child';
}

export function anchoredCharacterShift(
  preset: CharacterPreset,
  summary: VoiceSummary,
  autoMatch: boolean
): { pitch: number; formant: number } {
  if (preset.index === 0) return { pitch: 0, formant: 0 };
  const spkF0 = summary.ready ? summary.medianF0 : 0;
  if (!autoMatch || preset.targetF0 <= 0 || spkF0 <= 40) {
    return { pitch: preset.pitch, formant: preset.formant };
  }

  const maleSpeaker =
    summary.type !== 'Unknown'
      ? summary.type === 'Bass' || summary.type === 'Baritone' || summary.type === 'Tenor'
      : spkF0 < 165;

  let effTarget = preset.targetF0;
  if (maleSpeaker && preset.pitch > 3.0 && preset.targetF0 < 255) {
    effTarget = 270;
  }

  const adaptivePitch = 12 * Math.log2(effTarget / spkF0);
  let pitch = preset.pitch;
  if (!maleSpeaker && preset.pitch > 3.0 && preset.targetF0 < 235) {
    pitch = clamp(0.85 * adaptivePitch + 0.15 * preset.pitch, -2.0, 2.0);
  } else if (preset.pitch < 0) {
    const minP = Math.max(
      preset.pitch - 7.5,
      12 * Math.log2(Math.max(66, preset.targetF0 * 0.92) / spkF0)
    );
    const maxP = preset.pitch + 3.0;
    pitch = clamp(0.72 * adaptivePitch + 0.28 * preset.pitch, Math.min(minP, maxP), maxP);
  } else {
    const minP = maleSpeaker && preset.pitch > 3.0 ? Math.max(4.0, preset.pitch - 2.0) : preset.pitch - 3.0;
    const maxP = preset.pitch + 3.5;
    pitch = clamp(0.65 * adaptivePitch + 0.35 * preset.pitch, minP, maxP);
  }

  const spkTract = estimateSpeakerTractSemitones(summary);
  const formant = clamp(preset.formant - spkTract, preset.formant - 2.2, preset.formant + 2.0);
  return { pitch, formant };
}

/**
 * Applies a character preset cleanly onto StudioParams.
 * Fixes v2.0's double-offset link bug by returning exact target pitch/formant directly
 * (compensating for ageShape so effective output lands exactly on target).
 */
export function applyCharacterToParams(
  current: StudioParams,
  index: number,
  summary: VoiceSummary
): StudioParams {
  const preset = CHARACTER_PRESETS[clamp(Math.round(index), 0, CHARACTER_PRESETS.length - 1)];
  const { pitch: rawP, formant: rawF } = anchoredCharacterShift(preset, summary, current.autoMatch);
  const shape = ageShape(preset.age);
  const pitch = preset.index === 0 ? 0 : clamp(Number((rawP - shape.pitch).toFixed(2)), -18, 18);
  const formant = preset.index === 0 ? 0 : clamp(Number((rawF - shape.formant).toFixed(2)), -8, 8);

  return {
    ...current,
    character: preset.index,
    pitch,
    formant,
    age: preset.age,
    nasal: preset.nasal,
    body: preset.body,
    air: preset.air,
    regen: preset.regen,
    glottal: preset.glottal,
    growl: preset.growl,
    breath: preset.breath,
  };
}

export function voiceTypeLabel(type: VoiceType, lang: 'tr' | 'en'): string {
  switch (type) {
    case 'Bass':
      return lang === 'tr' ? 'Bas' : 'Bass';
    case 'Baritone':
      return lang === 'tr' ? 'Bariton' : 'Baritone';
    case 'Tenor':
      return 'Tenor';
    case 'Alto':
      return 'Alto';
    case 'MezzoSoprano':
      return lang === 'tr' ? 'Mezzosoprano' : 'Mezzo-soprano';
    case 'Soprano':
      return 'Soprano';
    case 'Child':
      return lang === 'tr' ? 'Çocuk' : 'Child';
    default:
      return '—';
  }
}

export function formatPitchNote(hz: number): string {
  if (hz <= 30 || !Number.isFinite(hz)) return '—';
  const midi = Math.round(69 + 12 * Math.log2(hz / 440));
  const note = NOTE_NAMES[((midi % 12) + 12) % 12];
  const octave = Math.floor(midi / 12) - 1;
  return `${note}${octave}`;
}

export function formatKeyScale(
  key: number,
  scale: DetectedScale | ScaleMode,
  lang: 'tr' | 'en'
): string {
  const note = NOTE_NAMES[((key % 12) + 12) % 12];
  if (scale === 'chromatic') return lang === 'tr' ? 'Kromatik' : 'Chromatic';
  if (scale === 'major') return `${note} Maj`;
  if (scale === 'minor') return `${note} Min`;
  if (scale === 'hijaz') return `${note} Hicaz`;
  if (scale === 'pentatonic') return `${note} Pent`;
  return lang === 'tr' ? 'Otomatik' : 'Auto';
}

export function formatSemitones(v: number): string {
  const rounded = Math.abs(v) < 0.04 ? 0 : v;
  return `${rounded > 0 ? '+' : ''}${rounded.toFixed(1)} st`;
}

export function formatPercent(v: number): string {
  return `${Math.round(v * 100)}%`;
}

export function formatSignedPercent(v: number): string {
  const p = Math.round(v * 100);
  return `${p > 0 ? '+' : ''}${p}%`;
}

export function formatDb(v: number): string {
  const rounded = Math.abs(v) < 0.04 ? 0 : v;
  return `${rounded > 0 ? '+' : ''}${rounded.toFixed(1)} dB`;
}
