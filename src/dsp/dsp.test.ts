import test from 'node:test';
import assert from 'node:assert/strict';
import { AutoTuneEngine, nearestScaleMidi } from './autoTuneEngine';
import { extractTrueCepstralEnvelope, warpFormantFreq } from './cepstralFormant';
import { applyCharacterToParams, CHARACTER_PRESETS, classifyVoice } from './characters';
import { detectPitchYin } from './pitchYin';
import {
  analyzeConsonantGuard,
  computeKWeightedLoudnessGain,
  computePsolaGainCompensation,
  encodeWav16Bit,
  LFGlottalTable,
  LookaheadLimiter,
  processVocalBufferOffline,
  runEngineeringDiagnostics,
} from './signalChain';
import { DEFAULT_STUDIO_PARAMS, type VoiceSummary } from './types';

test('YIN pitch detector locks cleanly onto 146.83 Hz voice despite heavy 42 Hz rumble', () => {
  const sr = 48000;
  const buf = new Float32Array(2048);
  const f0 = 146.83;
  for (let i = 0; i < buf.length; i++) {
    const t = i / sr;
    buf[i] =
      0.7 * Math.sin(2 * Math.PI * 42 * t) +
      0.5 * Math.sin(2 * Math.PI * f0 * t) +
      0.3 * Math.sin(2 * Math.PI * f0 * 2 * t) +
      0.15 * Math.sin(2 * Math.PI * f0 * 3 * t);
  }
  const res = detectPitchYin(buf, sr, { hpfHz: 80 });
  assert.equal(res.voiced, true);
  assert.ok(Math.abs(res.f0 - f0) < 1.5, `Expected ~${f0} Hz, got ${res.f0} Hz`);
});

test('Anatomical formant warping is strictly monotonic and scales F1/F2/F3 asymmetrically', () => {
  let prev = -1;
  for (let f = 80; f <= 8000; f += 100) {
    const warped = warpFormantFreq(f, -6.0);
    assert.ok(warped > prev, `Non-monotonic warp at ${f} Hz`);
    prev = warped;
  }
  assert.ok(warpFormantFreq(500, -4) < 420);
  assert.ok(warpFormantFreq(500, 4) > 580);
  assert.equal(warpFormantFreq(9000, -6), 9000);
});

test('True-envelope cepstral extractor returns valid F1 < F2 < F3 formants', () => {
  const sr = 48000;
  const buf = new Float32Array(1024);
  for (let i = 0; i < buf.length; i++) {
    const t = i / sr;
    buf[i] =
      0.6 * Math.sin(2 * Math.PI * 520 * t) +
      0.4 * Math.sin(2 * Math.PI * 1450 * t) +
      0.25 * Math.sin(2 * Math.PI * 2600 * t);
  }
  const analysis = extractTrueCepstralEnvelope(buf, sr, 130);
  assert.ok(analysis.formants[0] < analysis.formants[1]);
  assert.ok(analysis.formants[1] < analysis.formants[2]);
});

test('LF glottal wavetable is DC-balanced and unit-RMS', () => {
  const lf = new LFGlottalTable();
  let sum = 0;
  let sq = 0;
  const n = 512;
  for (let i = 0; i < n; i++) {
    const s = lf.sample(i / n, -0.4);
    sum += s;
    sq += s * s;
  }
  assert.ok(Math.abs(sum / n) < 0.005);
  assert.ok(Math.abs(Math.sqrt(sq / n) - 1.0) < 0.15);
});

test('AutoTuneEngine locks flat A4 (432 Hz) cleanly onto 440 Hz in A Major', () => {
  const at = new AutoTuneEngine();
  let cents = 0;
  for (let i = 0; i < 15; i++) {
    const r = at.update({
      f0In: 432,
      confidence: 0.92,
      pitchShiftSt: 0,
      amount: 1.0,
      retuneSpeedMs: 12,
      vibratoPreserve: 0,
      scaleMode: 'major',
      manualKey: 9,
      voiceType: 'Tenor',
      dtSec: 0.01,
    });
    cents = r.correctionCents;
  }
  assert.ok(cents > 28 && cents < 35, `Expected ~+31.8 cents, got ${cents}`);
  assert.equal(nearestScaleMidi(60.4, 0, 'major'), 60);
  assert.equal(nearestScaleMidi(61.2, 0, 'major'), 62);
});

test('LookaheadLimiter enforces strict -0.3 dBFS ceiling on overloaded signal', () => {
  const lim = new LookaheadLimiter(48000, -0.3, 3.0);
  let maxPeak = 0;
  for (let i = 0; i < 2048; i++) {
    const y = lim.processSample(2.5 * Math.sin((2 * Math.PI * 200 * i) / 48000));
    maxPeak = Math.max(maxPeak, Math.abs(y));
  }
  const peakDb = 20 * Math.log10(maxPeak);
  assert.ok(peakDb <= -0.28, `Limiter ceiling exceeded: ${peakDb.toFixed(2)} dBFS`);
  assert.ok(lim.consumeMaxGrDb() < -6);
});

test('Character presets apply cleanly without NaN and offline WAV export produces valid RIFF header', () => {
  const summary: VoiceSummary = {
    medianF0: 120,
    lowF0: 95,
    highF0: 155,
    formants: [480, 1380, 2450],
    brightness: 1600,
    breathiness: 0.12,
    seconds: 2.0,
    type: classifyVoice(120, [480, 1380, 2450]),
    ready: true,
  };
  assert.equal(summary.type, 'Baritone');
  assert.equal(classifyVoice(118, [534, 1547, 2528]), 'Baritone');
  assert.equal(classifyVoice(214, [660, 1780, 2820]), 'MezzoSoprano');

  const params = applyCharacterToParams(DEFAULT_STUDIO_PARAMS, 2, summary); // Giant
  assert.equal(params.character, 2);
  assert.ok(params.pitch < -4);

  const sr = 24000;
  const input = new Float32Array(sr * 0.25);
  for (let i = 0; i < input.length; i++) {
    input[i] = 0.4 * Math.sin((2 * Math.PI * 120 * i) / sr) + 0.2 * Math.sin((2 * Math.PI * 240 * i) / sr);
  }
  const rendered = processVocalBufferOffline(input, sr, params);
  assert.equal(rendered.output.length, input.length);
  for (let i = 0; i < rendered.output.length; i++) {
    assert.ok(Number.isFinite(rendered.output[i]));
  }
  assert.ok(rendered.peakDb <= -0.25);

  const wav = encodeWav16Bit(rendered.output, sr);
  assert.equal(String.fromCharCode(wav[0], wav[1], wav[2], wav[3]), 'RIFF');
  assert.equal(String.fromCharCode(wav[8], wav[9], wav[10], wav[11]), 'WAVE');
});

test('PSOLA compensation, K-weighted loudness, consonant guard, and full diagnostics pass', () => {
  assert.ok(computePsolaGainCompensation(400, 200) < 1);
  assert.ok(computePsolaGainCompensation(200, 400) > 1);
  const a = new Float32Array(512).fill(0.2);
  const b = new Float32Array(512).fill(0.1);
  assert.ok(computeKWeightedLoudnessGain(a, b, 48000) >= 0.35);

  const noise = new Float32Array(512);
  for (let i = 0; i < 512; i++) noise[i] = (i % 2 === 0 ? 1 : -1) * 0.25;
  const guard = analyzeConsonantGuard(noise, 48000, 0.1);
  assert.ok(guard.consonantProb > 0.5);

  const diag = runEngineeringDiagnostics(48000);
  assert.equal(diag.length, 6);
  for (const item of diag) {
    assert.equal(item.passed, true, `Diagnostic failed: ${item.id} (${item.measured})`);
  }
  assert.equal(CHARACTER_PRESETS.length, 13);
});
