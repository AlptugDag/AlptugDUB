import { LFGlottalTable } from '../dsp/signalChain';

export type BuiltInStemId = 'baritone' | 'mezzo' | 'vocal_melody';

export interface StemDescriptor {
  id: BuiltInStemId;
  nameTr: string;
  nameEn: string;
  subtitleTr: string;
  subtitleEn: string;
  durationSec: number;
}

export const BUILTIN_STEMS: readonly StemDescriptor[] = [
  {
    id: 'baritone',
    nameTr: 'Bariton Dublaj',
    nameEn: 'Baritone Dub',
    subtitleTr: '118 Hz · S/T ünsüzlü dramatik replik',
    subtitleEn: '118 Hz · Dramatic dialogue with S/T consonants',
    durationSec: 4.0,
  },
  {
    id: 'mezzo',
    nameTr: 'Mezzo Anlatıcı',
    nameEn: 'Mezzo Narrator',
    subtitleTr: '214 Hz · Net anlatım ve nefes geçişi',
    subtitleEn: '214 Hz · Crisp narration & breath phrasing',
    durationSec: 4.0,
  },
  {
    id: 'vocal_melody',
    nameTr: 'Vokal Melodi',
    nameEn: 'Vocal Melody',
    subtitleTr: '155–220 Hz · Hafif detone (Auto-Tune testi)',
    subtitleEn: '155–220 Hz · Slightly flat (Auto-Tune test)',
    durationSec: 4.2,
  },
];

interface ResonatorState {
  y1: number;
  y2: number;
}

function stepBandpass(
  x: number,
  freqHz: number,
  bwHz: number,
  sr: number,
  state: ResonatorState
): number {
  const r = Math.exp((-Math.PI * bwHz) / sr);
  const a1 = -2 * r * Math.cos((2 * Math.PI * freqHz) / sr);
  const a2 = r * r;
  const gain = (1 - r) * Math.sqrt(1 - 2 * r * Math.cos((2 * Math.PI * freqHz) / sr) + r * r);
  const y = gain * x - a1 * state.y1 - a2 * state.y2;
  state.y2 = state.y1;
  state.y1 = y;
  return y;
}

/**
 * Generates realistic multi-syllable formant-synthesized vocal test stems using our LF glottal source,
 * three parallel vocal tract formant resonators (F1/F2/F3), natural prosody/vibrato, and unvoiced
 * high-frequency sibilant/plosive consonant bursts (s, ş, t) so engineers can immediately audit
 * pitch shifting, formant warping, consonant guard, and Auto-Tune without a microphone.
 */
export function synthesizeVocalStem(stemId: BuiltInStemId, sampleRate = 48000): Float32Array {
  const desc = BUILTIN_STEMS.find((s) => s.id === stemId) ?? BUILTIN_STEMS[0];
  const total = Math.round(desc.durationSec * sampleRate);
  const out = new Float32Array(total);
  const lf = new LFGlottalTable();

  const r1: ResonatorState = { y1: 0, y2: 0 };
  const r2: ResonatorState = { y1: 0, y2: 0 };
  const r3: ResonatorState = { y1: 0, y2: 0 };
  const rSib: ResonatorState = { y1: 0, y2: 0 };

  let phase = 0;
  let rng = stemId === 'baritone' ? 104729 : stemId === 'mezzo' ? 224737 : 350377;
  const nextRand = () => {
    rng ^= rng << 13;
    rng ^= rng >>> 17;
    rng ^= rng << 5;
    return ((rng >>> 0) / 4294967296) * 2 - 1;
  };

  // Syllable timeline: [startSec, endSec, f0Start, f0End, F1, F2, F3, isConsonant]
  type Segment = [number, number, number, number, number, number, number, boolean];
  let segments: Segment[];
  let glottalOq = -0.25;

  if (stemId === 'baritone') {
    glottalOq = -0.35;
    segments = [
      [0.08, 0.22, 118, 118, 520, 1280, 2420, true], // "S..." sibilant onset
      [0.22, 0.88, 122, 114, 620, 1150, 2450, false], // "a" vowel
      [0.92, 1.04, 115, 115, 480, 1450, 2500, true], // "t/ş" consonant
      [1.04, 1.82, 130, 110, 440, 1780, 2520, false], // "e" vowel
      [1.92, 2.55, 116, 128, 360, 880, 2320, false], // "o" vowel
      [2.58, 2.74, 120, 120, 500, 1400, 2450, true], // "s" sibilant
      [2.74, 3.72, 124, 102, 580, 1220, 2380, false], // closing cadence
    ];
  } else if (stemId === 'mezzo') {
    glottalOq = 0.28;
    segments = [
      [0.08, 0.2, 215, 215, 650, 1650, 2850, true],
      [0.2, 0.92, 218, 232, 740, 1420, 2880, false],
      [0.96, 1.1, 220, 220, 550, 1800, 2900, true],
      [1.1, 1.9, 228, 206, 510, 2150, 2960, false],
      [2.0, 2.65, 208, 225, 430, 1080, 2750, false],
      [2.68, 2.82, 215, 215, 600, 1600, 2850, true],
      [2.82, 3.76, 222, 198, 690, 1500, 2820, false],
    ];
  } else {
    // Vocal melody in C Major (C3=130.8, E3=164.8, G3=196.0, A3=220.0, G3=196.0) intentionally ~28 cents flat/sharp for Auto-Tune testing
    glottalOq = 0.08;
    segments = [
      [0.1, 0.85, 128.8, 129.1, 610, 1340, 2560, false], // ~25 cents flat C3
      [0.9, 1.65, 167.2, 166.9, 490, 1860, 2650, false], // ~24 cents sharp E3
      [1.7, 2.45, 192.8, 193.2, 410, 990, 2480, false], // ~28 cents flat G3
      [2.5, 3.25, 216.5, 216.2, 650, 1260, 2550, false], // ~27 cents flat A3
      [3.3, 4.05, 193.0, 192.6, 540, 1520, 2580, false], // ~26 cents flat G3
    ];
  }

  for (let i = 0; i < total; i++) {
    const t = i / sampleRate;
    let sample = 0;

    for (let s = 0; s < segments.length; s++) {
      const [t0, t1, f0a, f0b, f1, f2, f3, isCons] = segments[s];
      if (t < t0 || t > t1) continue;
      const u = (t - t0) / Math.max(1e-4, t1 - t0);
      const env =
        Math.min(1, u / 0.12) * Math.min(1, (1 - u) / 0.15);

      if (isCons) {
        // Sibilant / fricative burst (5.5-8.5 kHz bandpass noise)
        const white = nextRand();
        const sib = stepBandpass(white, 6400, 2400, sampleRate, rSib);
        sample += sib * env * 0.24;
      } else {
        const vib = stemId === 'vocal_melody' ? Math.sin(2 * Math.PI * 5.6 * t) * 0.016 : Math.sin(2 * Math.PI * 5.1 * t) * 0.005;
        const f0 = (f0a + (f0b - f0a) * u) * (1 + vib);
        phase += f0 / sampleRate;
        if (phase >= 1) phase -= 1;

        const exc = lf.sample(phase, glottalOq) + nextRand() * 0.045;
        const y1 = stepBandpass(exc, f1, 85, sampleRate, r1) * 1.15;
        const y2 = stepBandpass(exc, f2, 125, sampleRate, r2) * 0.72;
        const y3 = stepBandpass(exc, f3, 180, sampleRate, r3) * 0.36;
        sample += (y1 + y2 + y3) * env * 0.26;
      }
    }
    out[i] = Math.max(-0.92, Math.min(0.92, sample));
  }

  return out;
}
