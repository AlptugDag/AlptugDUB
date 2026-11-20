import { clamp } from './characters';

export interface YinPitchResult {
  f0: number; // Hz, 0 if unvoiced
  period: number; // samples at native sampleRate
  aperiodicity: number; // YIN d'(tau) in 0..1
  confidence: number; // 0..1
  rms: number;
  voiced: boolean;
}

export interface BiquadCoeffs {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

/** Designs a 2nd-order Butterworth High-Pass Filter to strip sub-vocal rumble (<40..160 Hz). */
export function designHighPass(sampleRate: number, cutoffHz: number, q = 0.70710678): BiquadCoeffs {
  const fc = clamp(cutoffHz, 20, sampleRate * 0.42);
  const w0 = (2 * Math.PI * fc) / sampleRate;
  const cosW = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * q);
  const a0 = 1 + alpha;
  return {
    b0: ((1 + cosW) / 2) / a0,
    b1: -(1 + cosW) / a0,
    b2: ((1 + cosW) / 2) / a0,
    a1: (-2 * cosW) / a0,
    a2: (1 - alpha) / a0,
  };
}

/** Designs a 2nd-order Butterworth Low-Pass Filter. */
export function designLowPass(sampleRate: number, cutoffHz: number, q = 0.70710678): BiquadCoeffs {
  const fc = clamp(cutoffHz, 40, sampleRate * 0.45);
  const w0 = (2 * Math.PI * fc) / sampleRate;
  const cosW = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * q);
  const a0 = 1 + alpha;
  return {
    b0: ((1 - cosW) / 2) / a0,
    b1: (1 - cosW) / a0,
    b2: ((1 - cosW) / 2) / a0,
    a1: (-2 * cosW) / a0,
    a2: (1 - alpha) / a0,
  };
}

/** Applies a biquad filter cleanly over a copy of a Float32Array (zero side effects). */
export function applyBiquad(input: Float32Array, coeffs: BiquadCoeffs): Float32Array {
  const out = new Float32Array(input.length);
  let z1 = 0;
  let z2 = 0;
  const { b0, b1, b2, a1, a2 } = coeffs;
  for (let i = 0; i < input.length; i++) {
    const x = input[i];
    const y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    out[i] = y;
  }
  return out;
}

/**
 * Re-engineered YIN Pitch Detector (v3.0):
 *  - Pre-conditions input with a 2nd-order Butterworth HPF (default 75 Hz) so desk bumps / mic plosives
 *    can no longer hijack YIN into false 50-60 Hz locks (fixing Defect #6 from v2.0).
 *  - Uses octave-continuity bias against `prevF0` to suppress half/double-pitch glitches.
 */
export function detectPitchYin(
  frame: Float32Array,
  sampleRate: number,
  options?: {
    hpfHz?: number;
    minF0?: number;
    maxF0?: number;
    threshold?: number;
    prevF0?: number;
  }
): YinPitchResult {
  const minF0 = options?.minF0 ?? 55;
  const maxF0 = options?.maxF0 ?? 820;
  const threshold = options?.threshold ?? 0.2;
  const hpfHz = options?.hpfHz ?? 75;
  const prevF0 = options?.prevF0 ?? 0;

  // 1. Pre-conditioning HPF + DC removal (fixes sub-vocal rumble corruption)
  const conditioned = hpfHz > 20 ? applyBiquad(frame, designHighPass(sampleRate, hpfHz)) : frame;

  // Decimate toward ~12 kHz for crisp speech/vocal resolution & low CPU
  const decim = Math.max(1, Math.round(sampleRate / 12000));
  const dsr = sampleRate / decim;
  const decLen = Math.floor(conditioned.length / decim);

  // Simple boxcar anti-alias decimation after LP
  const lp = decim > 1 ? applyBiquad(conditioned, designLowPass(sampleRate, dsr * 0.42)) : conditioned;
  const dec = new Float32Array(decLen);
  for (let i = 0; i < decLen; i++) {
    dec[i] = lp[i * decim];
  }

  const maxLag = Math.min(Math.floor(decLen * 0.5) - 2, Math.ceil(dsr / minF0));
  const minLag = Math.max(2, Math.floor(dsr / maxF0));
  const win = Math.min(decLen - maxLag - 1, Math.round(dsr * 0.028));

  if (win < 16 || maxLag <= minLag) {
    return { f0: 0, period: 0, aperiodicity: 1, confidence: 0, rms: 0, voiced: false };
  }

  let energy = 0;
  for (let i = 0; i < win; i++) {
    energy += dec[i] * dec[i];
  }
  const rms = Math.sqrt(energy / win);
  if (rms < 1.2e-4) {
    return { f0: 0, period: 0, aperiodicity: 1, confidence: 0, rms, voiced: false };
  }

  // Difference function + cumulative mean normalized difference d'(tau)
  const diff = new Float32Array(maxLag + 2);
  diff[0] = 1;
  let running = 0;
  for (let tau = 1; tau <= maxLag; tau++) {
    let sum = 0;
    for (let i = 0; i < win; i++) {
      const delta = dec[i] - dec[i + tau];
      sum += delta * delta;
    }
    running += sum;
    diff[tau] = running > 1e-12 ? (sum * tau) / running : 1;
  }

  let minDiff = 1;
  let minTau = minLag;
  for (let tau = minLag; tau <= maxLag; tau++) {
    if (diff[tau] < minDiff) {
      minDiff = diff[tau];
      minTau = tau;
    }
  }

  // First local minimum under threshold with optional octave-continuity preference
  let best = -1;
  const expectedTau = prevF0 > minF0 && prevF0 < maxF0 ? dsr / prevF0 : 0;

  for (let tau = minLag; tau <= maxLag; tau++) {
    if (diff[tau] < threshold && diff[tau] <= minDiff + 0.085) {
      let t = tau;
      while (t + 1 <= maxLag && diff[t + 1] < diff[t]) t++;
      // If we have a previous voiced pitch and this dip is an octave away while a valid dip exists near prevF0, prefer continuity
      if (expectedTau > 0) {
        const ratio = t / expectedTau;
        const isOctaveJump = Math.abs(ratio - 2) < 0.14 || Math.abs(ratio - 0.5) < 0.09;
        if (isOctaveJump) {
          const nearIdx = clamp(Math.round(expectedTau), minLag, maxLag);
          let localBest = nearIdx;
          for (let k = Math.max(minLag, nearIdx - 4); k <= Math.min(maxLag, nearIdx + 4); k++) {
            if (diff[k] < diff[localBest]) localBest = k;
          }
          if (diff[localBest] < threshold * 1.15) {
            best = localBest;
            break;
          }
        }
      }
      best = t;
      break;
    }
  }

  if (best < 0 && minDiff < 0.3) {
    best = minTau;
  }

  if (best < 0) {
    const confidence = clamp(1 - minDiff / 0.45, 0, 1);
    return { f0: 0, period: 0, aperiodicity: minDiff, confidence, rms, voiced: false };
  }

  // Parabolic interpolation around best lag
  let tauRefined = best;
  if (best > minLag && best < maxLag) {
    const a = diff[best - 1];
    const b = diff[best];
    const c = diff[best + 1];
    const den = a - 2 * b + c;
    if (Math.abs(den) > 1e-12) {
      tauRefined = best + (0.5 * (a - c)) / den;
    }
  }

  const period = tauRefined * decim;
  const f0 = period > 0 ? sampleRate / period : 0;
  const aperiodicity = clamp(diff[best], 0, 1);
  const confidence = clamp(1 - aperiodicity / 0.45, 0, 1);
  const voiced = f0 >= minF0 && f0 <= maxF0 && aperiodicity < 0.32;

  return { f0: voiced ? f0 : 0, period: voiced ? period : 0, aperiodicity, confidence, rms, voiced };
}
