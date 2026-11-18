import { clamp } from './characters';

/** In-place radix-2 Cooley-Tukey FFT on split real/imag Float64Arrays (power-of-two length). */
export function fftRadix2(re: Float64Array, im: Float64Array, inverse = false): void {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i];
      re[i] = re[j];
      re[j] = tr;
      const ti = im[i];
      im[i] = im[j];
      im[j] = ti;
    }
  }

  const sign = inverse ? 1 : -1;
  for (let len = 2; len <= n; len <<= 1) {
    const half = len >> 1;
    const angle = (sign * 2 * Math.PI) / len;
    const wLenR = Math.cos(angle);
    const wLenI = Math.sin(angle);
    for (let i = 0; i < n; i += len) {
      let wR = 1;
      let wI = 0;
      for (let j = 0; j < half; j++) {
        const uR = re[i + j];
        const uI = im[i + j];
        const vR = re[i + j + half] * wR - im[i + j + half] * wI;
        const vI = re[i + j + half] * wI + im[i + j + half] * wR;
        re[i + j] = uR + vR;
        im[i + j] = uI + vI;
        re[i + j + half] = uR - vR;
        im[i + j + half] = uI - vI;
        const nextWR = wR * wLenR - wI * wLenI;
        wI = wR * wLenI + wI * wLenR;
        wR = nextWR;
      }
    }
  }

  if (inverse) {
    const invN = 1 / n;
    for (let i = 0; i < n; i++) {
      re[i] *= invN;
      im[i] *= invN;
    }
  }
}

/**
 * Anatomical non-linear F1/F2/F3/F4 vocal tract warp curve.
 * Shifts pharynx (F1), tongue body (F2), and lip/tip (F3) independently and relaxes cleanly above 8.5 kHz.
 */
export function warpFormantFreq(freqHz: number, formantSemitones: number): number {
  if (freqHz <= 0 || Math.abs(formantSemitones) < 1e-4) return freqHz;
  const d = clamp(formantSemitones, -10, 10);
  const d1 = d >= 0 ? 0.92 * d : 1.2 * d;
  const d2 = d >= 0 ? 1.18 * d : 0.9 * d;
  const d3 = d >= 0 ? 1.05 * d : 0.72 * d;
  const a1 = Math.pow(2, d1 / 12);
  const a2 = Math.pow(2, d2 / 12);
  const a3 = Math.pow(2, d3 / 12);

  const sx = [0, 250, 900, 2600, 3800, 8500, 24000];
  const sy = [
    0,
    250 * a1,
    900 * Math.sqrt(a1 * a2),
    2600 * Math.sqrt(a2 * a3),
    3800 * a3,
    8500,
    24000,
  ];
  // Guarantee strict monotonicity even at extreme negative shifts
  for (let i = 1; i < sy.length; i++) {
    sy[i] = Math.max(sy[i], sy[i - 1] + 25);
  }
  if (freqHz >= sx[sx.length - 1]) return freqHz;
  for (let i = 0; i < sx.length - 1; i++) {
    if (freqHz <= sx[i + 1]) {
      const t = (freqHz - sx[i]) / (sx[i + 1] - sx[i]);
      return sy[i] + t * (sy[i + 1] - sy[i]);
    }
  }
  return freqHz;
}

export interface CepstralAnalysis {
  magnitudes: Float32Array;
  envelope: Float32Array; // True-envelope linear magnitude
  formants: [number, number, number]; // F1, F2, F3 in Hz
  centroidHz: number;
}

/**
 * Re-engineered True-Envelope Cepstral Extractor (v3.0):
 *  - Uses 2x zero-padded FFT (fixes Defect #1 circular convolution wrap-around aliasing).
 *  - Uses F0-adaptive quefrency liftering (fixes Defect #4 fixed 1.25ms low-F0 formant smearing).
 *  - Runs 3 iterative Imai/Röbel true-envelope passes so the curve hugs harmonic peaks instead of
 *    sinking into inter-harmonic valleys.
 */
export function extractTrueCepstralEnvelope(
  samples: Float32Array,
  sampleRate: number,
  f0Hz: number
): CepstralAnalysis {
  const winLen = samples.length;
  // 2x zero-padded power-of-two FFT for alias-free linear cepstral/spectral analysis
  let fftSize = 512;
  while (fftSize < winLen * 2) fftSize <<= 1;
  const halfBins = (fftSize >> 1) + 1;
  const binHz = sampleRate / fftSize;

  const re = new Float64Array(fftSize);
  const im = new Float64Array(fftSize);
  for (let i = 0; i < winLen; i++) {
    const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / Math.max(1, winLen - 1));
    re[i] = samples[i] * w;
  }
  fftRadix2(re, im, false);

  const magnitudes = new Float32Array(halfBins);
  const logMag = new Float64Array(halfBins);
  let num = 0;
  let den = 0;
  for (let k = 0; k < halfBins; k++) {
    const m = Math.hypot(re[k], im[k]);
    magnitudes[k] = m;
    logMag[k] = Math.log(Math.max(1e-7, m));
    num += k * binHz * m;
    den += m;
  }

  // F0-adaptive quefrency cutoff: tracks 0.72 * T0 cleanly across Bass (80 Hz) through Child (420 Hz)
  const guideF0 = f0Hz > 50 ? f0Hz : 150;
  const adaptiveQ = Math.round((0.72 * sampleRate) / Math.max(65, guideF0));
  const maxQ = clamp(adaptiveQ, 14, Math.min(fftSize >> 3, Math.round(0.0055 * sampleRate)));
  const taperStart = Math.max(6, Math.floor(maxQ * 0.72));

  // Iterative True-Envelope cepstral liftering (3 iterations)
  const workLog = new Float64Array(logMag);
  const cepR = new Float64Array(fftSize);
  const cepI = new Float64Array(fftSize);

  for (let iter = 0; iter < 3; iter++) {
    cepR.fill(0);
    cepI.fill(0);
    for (let k = 0; k < halfBins; k++) {
      cepR[k] = workLog[k];
      if (k > 0 && k < fftSize >> 1) cepR[fftSize - k] = workLog[k];
    }
    fftRadix2(cepR, cepI, true);

    for (let q = taperStart; q <= maxQ; q++) {
      const u = (q - taperStart) / Math.max(1, maxQ - taperStart);
      const w = 0.5 * (1 + Math.cos(Math.PI * u));
      cepR[q] *= w;
      if (q > 0) cepR[fftSize - q] *= w;
    }
    for (let q = maxQ + 1; q < fftSize - maxQ; q++) {
      cepR[q] = 0;
      cepI[q] = 0;
    }
    fftRadix2(cepR, cepI, false);

    for (let k = 0; k < halfBins; k++) {
      // Peak-hugging update: keep max(original logMag, smoothed envelope)
      workLog[k] = Math.max(logMag[k], cepR[k]);
    }
  }

  const envelope = new Float32Array(halfBins);
  for (let k = 0; k < halfBins; k++) {
    envelope[k] = Math.exp(clamp(cepR[k], -16, 8));
  }

  // Extract F1 (240..950 Hz), F2 (850..2650 Hz), F3 (2200..3850 Hz) peaks cleanly
  const findPeakInBand = (fLo: number, fHi: number, fallback: number): number => {
    const kLo = clamp(Math.floor(fLo / binHz), 2, halfBins - 3);
    const kHi = clamp(Math.ceil(fHi / binHz), kLo + 1, halfBins - 2);
    let bestK = -1;
    let bestVal = -Infinity;
    for (let k = kLo; k <= kHi; k++) {
      const prev = cepR[k - 1];
      const cur = cepR[k];
      const next = cepR[k + 1];
      if (cur > prev && cur >= next && cur > bestVal) {
        bestVal = cur;
        bestK = k;
      }
    }
    if (bestK < 0) {
      // Fallback to highest energy bin in band
      for (let k = kLo; k <= kHi; k++) {
        if (cepR[k] > bestVal) {
          bestVal = cepR[k];
          bestK = k;
        }
      }
    }
    if (bestK <= 1 || bestK >= halfBins - 1) return fallback;
    const a = cepR[bestK - 1];
    const b = cepR[bestK];
    const c = cepR[bestK + 1];
    const denom = a - 2 * b + c;
    const off = Math.abs(denom) > 1e-9 ? clamp((0.5 * (a - c)) / denom, -0.5, 0.5) : 0;
    return (bestK + off) * binHz;
  };

  const f1 = findPeakInBand(240, 950, 500);
  const f2 = findPeakInBand(Math.max(850, f1 + 320), 2650, 1480);
  const f3 = findPeakInBand(Math.max(2100, f2 + 450), 3900, 2550);

  return {
    magnitudes,
    envelope,
    formants: [f1, f2, f3],
    centroidHz: den > 1e-9 ? num / den : 0,
  };
}

/**
 * Warps a linear spectral envelope using the non-linear anatomical F1/F2/F3/F4 curve.
 */
export function warpSpectralEnvelope(
  envelope: Float32Array,
  sampleRate: number,
  formantSemitones: number
): Float32Array {
  const halfBins = envelope.length;
  const fftSize = (halfBins - 1) * 2;
  const binHz = sampleRate / fftSize;
  const out = new Float32Array(halfBins);

  if (Math.abs(formantSemitones) < 0.01) {
    out.set(envelope);
    return out;
  }

  // Build monotonic forward mapping then invert via binary search for exact continuity
  const fwd = new Float32Array(halfBins);
  for (let k = 0; k < halfBins; k++) {
    fwd[k] = warpFormantFreq(k * binHz, formantSemitones) / binHz;
  }

  let src = 0;
  out[0] = envelope[0];
  for (let k = 1; k < halfBins; k++) {
    while (src + 1 < halfBins - 1 && fwd[src + 1] < k) src++;
    const f0 = fwd[src];
    const f1 = fwd[Math.min(halfBins - 1, src + 1)];
    const frac = f1 > f0 + 1e-6 ? clamp((k - f0) / (f1 - f0), 0, 1) : 0;
    const log0 = Math.log(Math.max(1e-7, envelope[src]));
    const log1 = Math.log(Math.max(1e-7, envelope[Math.min(halfBins - 1, src + 1)]));
    out[k] = Math.exp(log0 + (log1 - log0) * frac);
  }
  return out;
}

/** Downsamples a linear magnitude spectrum into 64 log-spaced dB bins (60 Hz .. 16 kHz) for UI rendering. */
export function spectrumToLogBins(
  magnitudes: Float32Array,
  sampleRate: number,
  numBins = 64
): number[] {
  const halfBins = magnitudes.length;
  const fftSize = (halfBins - 1) * 2;
  const binHz = sampleRate / fftSize;
  const minHz = 60;
  const maxHz = Math.min(16000, sampleRate * 0.46);
  const logMin = Math.log(minHz);
  const logMax = Math.log(maxHz);
  const out = new Array<number>(numBins);

  for (let i = 0; i < numBins; i++) {
    const f0 = Math.exp(logMin + ((logMax - logMin) * i) / numBins);
    const f1 = Math.exp(logMin + ((logMax - logMin) * (i + 1)) / numBins);
    const k0 = clamp(Math.floor(f0 / binHz), 1, halfBins - 1);
    const k1 = clamp(Math.ceil(f1 / binHz), k0, halfBins - 1);
    let peak = 1e-6;
    for (let k = k0; k <= k1; k++) {
      if (magnitudes[k] > peak) peak = magnitudes[k];
    }
    const db = 20 * Math.log10(peak / (fftSize * 0.25) + 1e-6);
    out[i] = clamp(db, -78, 0);
  }
  return out;
}
