import { clamp } from './characters';
import type { DetectedScale, ScaleMode, VoiceType } from './types';

export interface AutoTuneResult {
  correctionSemitones: number;
  correctionCents: number;
  key: number; // 0=C .. 11=B
  scale: DetectedScale;
  targetMidi: number;
}

const MAJOR_PROFILE = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const MINOR_PROFILE = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];

const SCALE_INTERVALS: Record<Exclude<ScaleMode, 'auto' | 'chromatic'>, readonly number[]> = {
  major: [0, 2, 4, 5, 7, 9, 11],
  minor: [0, 2, 3, 5, 7, 8, 10],
  hijaz: [0, 1, 4, 5, 7, 8, 10],
  pentatonic: [0, 2, 4, 7, 9],
};

/**
 * Re-engineered Studio Auto-Tune Engine (v3.0):
 *  - Fixes Defect #5a: Supports fractional pitch shifts accurately without integer-rounding key mismatch.
 *  - Fixes Defect #5b: Fast melisma onset detection breaks the 90 ms note-center lag immediately on >0.65 st leaps.
 *  - Adds independent Retune Speed (5..120 ms), Vibrato Preservation (0..100%), and manual Scale/Key Lock
 *    (Major, Minor, Chromatic, Hijaz, Pentatonic) alongside Krumhansl-Schmuckler Auto-Detect.
 */
export class AutoTuneEngine {
  private chroma = new Float64Array(12);
  private totalChroma = 0;
  private detectedInKey = 0;
  private detectedScale: DetectedScale = 'chromatic';
  private centerMidi = 0;
  private prevMidi = 0;
  private lockedTargetMidi = 60;
  private hasTarget = false;
  private wasVoiced = false;
  private smoothDelta = 0;
  private lastResult: AutoTuneResult = {
    correctionSemitones: 0,
    correctionCents: 0,
    key: 0,
    scale: 'chromatic',
    targetMidi: 60,
  };

  reset(): void {
    this.chroma.fill(0);
    this.totalChroma = 0;
    this.detectedInKey = 0;
    this.detectedScale = 'chromatic';
    this.centerMidi = 0;
    this.prevMidi = 0;
    this.lockedTargetMidi = 60;
    this.hasTarget = false;
    this.wasVoiced = false;
    this.smoothDelta = 0;
    this.lastResult = {
      correctionSemitones: 0,
      correctionCents: 0,
      key: 0,
      scale: 'chromatic',
      targetMidi: 60,
    };
  }

  resetProfile(): void {
    this.chroma.fill(0);
    this.totalChroma = 0;
    this.detectedInKey = 0;
    this.detectedScale = 'chromatic';
  }

  update(params: {
    f0In: number;
    confidence: number;
    pitchShiftSt: number;
    amount: number;
    retuneSpeedMs: number;
    vibratoPreserve: number;
    scaleMode: ScaleMode;
    manualKey: number;
    voiceType: VoiceType;
    dtSec: number;
  }): AutoTuneResult {
    const {
      f0In,
      confidence,
      pitchShiftSt,
      amount,
      retuneSpeedMs,
      vibratoPreserve,
      scaleMode,
      manualKey,
      voiceType,
      dtSec,
    } = params;

    const voiced = f0In > 45 && f0In < 1050 && confidence >= 0.42;
    const dt = clamp(dtSec, 0.0005, 0.05);

    if (voiced) {
      const midiIn = 69 + 12 * Math.log2(f0In / 440);
      const dMidi = this.wasVoiced ? Math.abs(midiIn - this.prevMidi) : 0;

      // 1. Update Krumhansl-Schmuckler 12-bin chroma histogram on stable voiced frames
      if (confidence > 0.55 && dMidi < 0.75) {
        const keep = this.totalChroma > 12 ? Math.exp(-dt / 25) : 1;
        for (let i = 0; i < 12; i++) this.chroma[i] *= keep;
        this.totalChroma *= keep;

        const pcFloat = ((midiIn % 12) + 12) % 12;
        const pc0 = Math.floor(pcFloat) % 12;
        const pc1 = (pc0 + 1) % 12;
        const frac = pcFloat - Math.floor(pcFloat);
        const w = confidence * dt * 20;
        const tuneWeight = 1 - 0.5 * Math.sin(Math.PI * frac);
        this.chroma[pc0] += w * (1 - frac) * tuneWeight;
        this.chroma[pc1] += w * frac * tuneWeight;
        this.totalChroma += w * tuneWeight;

        if (this.totalChroma >= 3.2) {
          this.detectKeyAndScale();
        }
      }

      // 2. Melisma-aware note-center separation: fast jump (>0.68 st step) resets center immediately
      if (!this.wasVoiced || Math.abs(midiIn - this.centerMidi) > 1.35 || dMidi > 0.68) {
        this.centerMidi = midiIn;
        this.hasTarget = false;
      } else {
        const kCenter = 1 - Math.exp(-dt / 0.075);
        this.centerMidi += (midiIn - this.centerMidi) * kCenter;
      }
      const vibratoMidi = clamp(midiIn - this.centerMidi, -0.75, 0.75);
      this.prevMidi = midiIn;

      // 3. Determine active scale and key (fractional shift handled seamlessly in output pitch domain)
      const isAuto = scaleMode === 'auto';
      const activeScaleMode: ScaleMode = isAuto
        ? this.totalChroma >= 3.2
          ? this.detectedScale
          : 'chromatic'
        : scaleMode;

      // Shift output key when in auto mode; honor exact manualKey when user locked a session key
      const outKey = isAuto
        ? (((this.detectedInKey + Math.round(pitchShiftSt)) % 12) + 12) % 12
        : ((manualKey % 12) + 12) % 12;

      const shiftedCenter = this.centerMidi + pitchShiftSt;
      const candTarget = nearestScaleMidi(shiftedCenter, outKey, activeScaleMode);

      if (!this.hasTarget) {
        this.lockedTargetMidi = candTarget;
        this.hasTarget = true;
      } else if (Math.abs(candTarget - this.lockedTargetMidi) > 0.5) {
        const distOld = Math.abs(shiftedCenter - this.lockedTargetMidi);
        const distNew = Math.abs(shiftedCenter - candTarget);
        if (distNew + 0.14 < distOld) {
          this.lockedTargetMidi = candTarget;
        }
      }

      // 4. Independent Retune Speed + Vibrato Preservation physics
      if (amount <= 0.005) {
        this.smoothDelta *= Math.exp(-dt / 0.02);
      } else {
        const voiceScale = voiceType === 'Bass' || voiceType === 'Baritone' ? 1.2 : 1.0;
        const tauSec = clamp((retuneSpeedMs / 1000) * voiceScale, 0.004, 0.14);
        const kRetune = 1 - Math.exp(-dt / tauSec);

        const vibratoKeep = clamp(vibratoPreserve * (1 - 0.35 * Math.max(0, amount - 0.7) / 0.3), 0, 1);
        const desiredMidi = this.lockedTargetMidi + vibratoMidi * vibratoKeep;
        const rawCorr = desiredMidi - (midiIn + pitchShiftSt);
        const voicedGain = clamp((confidence - 0.4) / 0.25, 0, 1);
        const targetCorr = clamp(rawCorr * amount * voicedGain, -4, 4);
        this.smoothDelta += (targetCorr - this.smoothDelta) * kRetune;
      }

      const displayScale: DetectedScale =
        activeScaleMode === 'major'
          ? 'major'
          : activeScaleMode === 'minor'
            ? 'minor'
            : activeScaleMode === 'chromatic'
              ? 'chromatic'
              : 'minor';

      this.lastResult = {
        correctionSemitones: this.smoothDelta,
        correctionCents: this.smoothDelta * 100,
        key: outKey,
        scale: displayScale,
        targetMidi: this.lockedTargetMidi,
      };
    } else {
      this.smoothDelta *= Math.exp(-dt / 0.022);
      this.hasTarget = false;
      this.lastResult = {
        ...this.lastResult,
        correctionSemitones: this.smoothDelta,
        correctionCents: this.smoothDelta * 100,
      };
    }

    this.wasVoiced = voiced;
    return this.lastResult;
  }

  private detectKeyAndScale(): void {
    let cMean = 0;
    for (let i = 0; i < 12; i++) cMean += this.chroma[i];
    cMean /= 12;

    const corrWith = (root: number, profile: readonly number[]): number => {
      let pMean = 0;
      for (let i = 0; i < 12; i++) pMean += profile[i];
      pMean /= 12;

      let num = 0;
      let dc = 0;
      let dp = 0;
      for (let i = 0; i < 12; i++) {
        const c = this.chroma[(root + i) % 12] - cMean;
        const p = profile[i] - pMean;
        num += c * p;
        dc += c * c;
        dp += p * p;
      }
      const den = Math.sqrt(dc * dp);
      return den > 1e-9 ? num / den : 0;
    };

    let bestCorr = -2;
    let bestKey = 0;
    let bestScale: DetectedScale = 'major';

    for (let k = 0; k < 12; k++) {
      const cMaj = corrWith(k, MAJOR_PROFILE);
      if (cMaj > bestCorr) {
        bestCorr = cMaj;
        bestKey = k;
        bestScale = 'major';
      }
      const cMin = corrWith(k, MINOR_PROFILE);
      if (cMin > bestCorr) {
        bestCorr = cMin;
        bestKey = k;
        bestScale = 'minor';
      }
    }

    this.detectedInKey = bestKey;
    this.detectedScale = bestCorr > 0.16 ? bestScale : 'chromatic';
  }
}

export function nearestScaleMidi(midi: number, key: number, scale: ScaleMode): number {
  if (scale === 'chromatic' || scale === 'auto') {
    return Math.round(midi);
  }
  const steps = SCALE_INTERVALS[scale];
  const baseOct = Math.floor((midi - key) / 12);
  let best = Math.round(midi);
  let bestDist = 1e6;

  for (let oct = baseOct - 1; oct <= baseOct + 1; oct++) {
    for (const st of steps) {
      const cand = oct * 12 + key + st;
      const dist = Math.abs(midi - cand);
      if (dist < bestDist) {
        bestDist = dist;
        best = cand;
      }
    }
  }
  return best;
}
