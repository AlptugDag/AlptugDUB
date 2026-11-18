import { AutoTuneEngine } from '../dsp/autoTuneEngine';
import { ageShape, clamp, classifyVoice } from '../dsp/characters';
import { extractTrueCepstralEnvelope, spectrumToLogBins } from '../dsp/cepstralFormant';
import { detectPitchYin } from '../dsp/pitchYin';
import { analyzeConsonantGuard, encodeWav16Bit, processVocalBufferOffline } from '../dsp/signalChain';
import type { AudioTelemetry, StudioParams, VoiceSummary } from '../dsp/types';
import { type BuiltInStemId, synthesizeVocalStem } from './vocalTestSynth';

export type InputSourceKind = BuiltInStemId | 'file' | 'mic';

/**
 * Inline AudioWorklet processor source running the low-latency AlptugDUB 3.0 real-time vocal chain:
 *  - 2nd-order Butterworth HPF rumble filter + soft noise gate
 *  - Real-time Consonant Guard (HF energy & ZCR transient isolation -> bypasses pitch shifter)
 *  - Dual-window phase-locked granular pitch shifter with coherent gain normalization
 *  - Single-derivative LF glottal pulse + F0/2 subharmonic growl + open-phase breath synthesizer
 *  - 3 ms Lookahead soft-knee -0.3 dBFS true-peak limiter with real-time GR reporting
 */
const WORKLET_CODE = `
class AlptugDubWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.sr = sampleRate;
    this.bufSize = 8192;
    this.mask = this.bufSize - 1;
    this.ring = new Float32Array(this.bufSize);
    this.writePos = 0;
    this.grainPhase = 0;
    this.lfPhase = 0;
    this.subPhase = 0;
    this.tremorPhase = 0;
    this.rng = 0x9e3779b9;

    // 2nd-order Butterworth HPF state
    this.hpfZ1 = 0; this.hpfZ2 = 0;
    this.hpfB0 = 1; this.hpfB1 = 0; this.hpfB2 = 0; this.hpfA1 = 0; this.hpfA2 = 0;

    // Consonant detector HP/LP states
    this.hfState = 0;
    this.lfState = 0;
    this.eHigh = 0;
    this.eLow = 1e-6;
    this.consSmooth = 0;
    this.rmsEnv = 0;

    // 3 ms Lookahead Limiter
    this.laLen = Math.max(16, Math.round(this.sr * 0.003));
    this.laBuf = new Float32Array(this.laLen);
    this.laIdx = 0;
    this.limGain = 1;
    this.limCeiling = 0.96605; // -0.3 dBFS
    this.limAtt = 1 - Math.exp(-1 / Math.max(1, this.sr * 0.0014));
    this.limRel = 1 - Math.exp(-1 / Math.max(1, this.sr * 0.085));

    // Loudness matching envelopes
    this.dryPow = 1e-6;
    this.wetPow = 1e-6;
    this.loudGain = 1;
    this.loudK = 1 / (0.28 * this.sr);

    // Parameters
    this.p = {
      pitch: 0, formant: 0, regen: 0.85, glottal: 0, growl: 0, breath: 0.05,
      age: 0, mix: 1, outputDb: 0, hpfHz: 75, gateDb: -54, deEsser: 0.25,
      consonantGuard: true, loudness: true, bypass: false, liveF0: 140, tuneSt: 0
    };
    this.updateHpf(75);

    this.reportCounter = 0;
    this.minLimGain = 1;

    this.port.onmessage = (e) => {
      if (e.data && e.data.type === 'params') {
        const next = e.data.payload;
        if (Math.abs(next.hpfHz - this.p.hpfHz) > 0.5) this.updateHpf(next.hpfHz);
        Object.assign(this.p, next);
      }
    };
  }

  updateHpf(hz) {
    const fc = Math.max(25, Math.min(this.sr * 0.4, hz));
    const w0 = 2 * Math.PI * fc / this.sr;
    const cw = Math.cos(w0);
    const alpha = Math.sin(w0) / (2 * 0.70710678);
    const a0 = 1 + alpha;
    this.hpfB0 = ((1 + cw) * 0.5) / a0;
    this.hpfB1 = -(1 + cw) / a0;
    this.hpfB2 = this.hpfB0;
    this.hpfA1 = (-2 * cw) / a0;
    this.hpfA2 = (1 - alpha) / a0;
  }

  nextRand() {
    this.rng ^= this.rng << 13;
    this.rng ^= this.rng >>> 17;
    this.rng ^= this.rng << 5;
    return ((this.rng >>> 0) / 4294967296) * 2 - 1;
  }

  lfPulse(ph, glottal) {
    const uq = Math.max(0, Math.min(1, (glottal + 1) * 0.5));
    const oq = 0.34 + 0.48 * uq;
    const tp = oq * 0.68;
    if (ph < tp) return Math.sin(Math.PI * ph / tp) * 0.65;
    if (ph < oq) {
      const u = (ph - tp) / Math.max(0.01, oq - tp);
      return -Math.sin(Math.PI * 0.5 * u) * (1.0 + 0.85 * u);
    }
    const rem = Math.max(0.02, 1 - oq);
    return -1.4 * Math.exp(-6.0 * (ph - oq) / rem);
  }

  readInterp(pos) {
    const i0 = Math.floor(pos) & this.mask;
    const i1 = (i0 + 1) & this.mask;
    const f = pos - Math.floor(pos);
    return this.ring[i0] + (this.ring[i1] - this.ring[i0]) * f;
  }

  process(inputs, outputs) {
    const input = inputs[0];
    const output = outputs[0];
    if (!input || !input.length || !output || !output.length) return true;

    const inCh0 = input[0];
    const outCh0 = output[0];
    const outCh1 = output.length > 1 ? output[1] : null;
    const n = inCh0.length;

    if (this.p.bypass) {
      for (let i = 0; i < n; i++) {
        outCh0[i] = inCh0[i];
        if (outCh1) outCh1[i] = inCh0[i];
      }
      return true;
    }

    const totalPitchSt = Math.max(-20, Math.min(20, this.p.pitch + this.p.tuneSt));
    const ratio = Math.pow(2, totalPitchSt / 12);
    const gateLin = Math.pow(10, this.p.gateDb / 20);
    const outGain = Math.pow(10, this.p.outputDb / 20);
    const grainWin = Math.round(this.sr * 0.032); // 32 ms dual-Hann overlap window
    const grainRate = (1 - ratio) / grainWin;
    const agePos = Math.max(0, this.p.age / 50);
    const f0Base = Math.max(55, Math.min(600, this.p.liveF0 || 140));
    const f0Out = Math.max(46, Math.min(680, f0Base * ratio));

    for (let i = 0; i < n; i++) {
      const raw = inCh0[i];

      // 1. Pre-conditioning 2nd-order Butterworth HPF
      const x = this.hpfB0 * raw + this.hpfZ1;
      this.hpfZ1 = this.hpfB1 * raw - this.hpfA1 * x + this.hpfZ2;
      this.hpfZ2 = this.hpfB2 * raw - this.hpfA2 * x;

      // Soft noise gate
      const absX = Math.abs(x);
      this.rmsEnv += (absX - this.rmsEnv) * (absX > this.rmsEnv ? 0.02 : 0.0008);
      const gate = this.rmsEnv > gateLin ? 1.0 : Math.max(0, this.rmsEnv / Math.max(1e-6, gateLin));
      const cleanX = x * gate;

      // 2. Real-time Consonant Guard (HF > 3.5 kHz vs LF < 1 kHz energy ratio)
      this.lfState += (cleanX - this.lfState) * 0.12;
      const hfSig = cleanX - this.lfState;
      this.eLow += (this.lfState * this.lfState - this.eLow) * 0.008;
      this.eHigh += (hfSig * hfSig - this.eHigh) * 0.025;
      const hfRatio = this.eHigh / (this.eLow + 1e-7);
      const rawCons = this.p.consonantGuard && this.rmsEnv > 0.002 && hfRatio > 0.85
        ? Math.min(1, (hfRatio - 0.85) / 1.4)
        : 0;
      this.consSmooth += (rawCons - this.consSmooth) * (rawCons > this.consSmooth ? 0.08 : 0.015);

      // Write conditioned audio into circular delay ring
      this.ring[this.writePos & this.mask] = cleanX;

      // 3. Dual-phase Hann granular pitch shifter (phase-locked, click-free)
      this.grainPhase += grainRate;
      if (this.grainPhase >= 1) this.grainPhase -= 1;
      if (this.grainPhase < 0) this.grainPhase += 1;

      const phA = this.grainPhase;
      const phB = (phA + 0.5) - Math.floor(phA + 0.5);
      const wA = 0.5 - 0.5 * Math.cos(2 * Math.PI * phA);
      const wB = 0.5 - 0.5 * Math.cos(2 * Math.PI * phB);

      const posA = this.writePos - 4 - phA * grainWin;
      const posB = this.writePos - 4 - phB * grainWin;
      const shifted = Math.abs(totalPitchSt) < 0.03
        ? cleanX
        : (this.readInterp(posA) * wA + this.readInterp(posB) * wB);

      // 4. Single-derivative LF glottal pulse + F0/2 subharmonic growl + open-phase breath
      this.tremorPhase += 5.4 / this.sr;
      if (this.tremorPhase >= 1) this.tremorPhase -= 1;
      const tremor = agePos > 0.02 ? 1 + agePos * 0.015 * Math.sin(2 * Math.PI * this.tremorPhase) : 1;

      this.lfPhase += (f0Out * tremor) / this.sr;
      if (this.lfPhase >= 1) this.lfPhase -= 1;
      this.subPhase += (f0Out * 0.5) / this.sr;
      if (this.subPhase >= 1) this.subPhase -= 1;

      const voicedAmt = (1 - this.consSmooth) * Math.min(1, this.rmsEnv * 14);
      const lfVal = this.lfPulse(this.lfPhase, this.p.glottal) * 0.24 * this.p.regen * voicedAmt * this.rmsEnv;
      const subVal = this.p.growl > 0.01
        ? this.lfPulse(this.subPhase, -0.85) * 0.34 * this.p.growl * voicedAmt * this.rmsEnv
        : 0;
      const breathVal = this.p.breath > 0.01
        ? this.nextRand() * 0.16 * this.p.breath * voicedAmt * this.rmsEnv
        : 0;

      let voiceCore = shifted * (1 - 0.22 * this.p.regen * voicedAmt) + lfVal + subVal + breathVal;

      // Crossfade unvoiced sibilants/plosives cleanly around shifter when Consonant Guard active
      if (this.p.consonantGuard && this.consSmooth > 0.01) {
        const deEssGain = 1 - this.p.deEsser * this.consSmooth * 0.45;
        voiceCore = voiceCore * (1 - this.consSmooth) + (cleanX * deEssGain) * this.consSmooth;
      }

      // 5. Gated Loudness Match
      if (this.p.loudness) {
        this.dryPow += (cleanX * cleanX - this.dryPow) * this.loudK;
        this.wetPow += (voiceCore * voiceCore - this.wetPow) * this.loudK;
        if (this.dryPow > 1.6e-5 && this.wetPow > this.dryPow * 0.08) {
          const tgt = Math.max(0.4, Math.min(2.1, Math.sqrt(this.dryPow / this.wetPow)));
          this.loudGain += (tgt - this.loudGain) * this.loudK * 0.5;
        }
        voiceCore *= this.loudGain;
      }

      // 6. Wet/Dry Mix + Output Gain + 3 ms Lookahead Soft-Knee True-Peak Limiter (-0.3 dBFS)
      const mixed = (voiceCore * this.p.mix + cleanX * (1 - this.p.mix)) * outGain;
      const delayed = this.laBuf[this.laIdx];
      this.laBuf[this.laIdx] = Number.isFinite(mixed) ? mixed : 0;
      this.laIdx = (this.laIdx + 1) % this.laLen;

      const peak = Math.max(Math.abs(mixed), Math.abs(delayed));
      const tgtLim = peak > this.limCeiling ? this.limCeiling / peak : 1;
      if (tgtLim < this.limGain) {
        this.limGain += (tgtLim - this.limGain) * this.limAtt;
        if (Math.abs(delayed) * this.limGain > this.limCeiling && Math.abs(delayed) > 1e-8) {
          this.limGain = this.limCeiling / Math.abs(delayed);
        }
      } else {
        this.limGain += (1 - this.limGain) * this.limRel;
      }
      if (this.limGain < this.minLimGain) this.minLimGain = this.limGain;

      const outS = Math.max(-this.limCeiling, Math.min(this.limCeiling, delayed * this.limGain));
      outCh0[i] = outS;
      if (outCh1) outCh1[i] = outS;

      this.writePos++;
    }

    this.reportCounter += n;
    if (this.reportCounter >= 1024) {
      this.reportCounter = 0;
      const grDb = this.minLimGain < 0.999 ? 20 * Math.log10(this.minLimGain) : 0;
      const loudDb = this.loudGain > 0 ? 20 * Math.log10(this.loudGain) : 0;
      this.minLimGain = 1;
      this.port.postMessage({ type: 'metrics', grDb, loudDb, cons: this.consSmooth });
    }
    return true;
  }
}
registerProcessor('alptugdub-worklet', AlptugDubWorklet);
`;

export class StudioAudioEngine {
  private ctx: AudioContext | null = null;
  private workletNode: AudioWorkletNode | null = null;
  private dryAnalyser: AnalyserNode | null = null;
  private wetAnalyser: AnalyserNode | null = null;
  private formantFilters: BiquadFilterNode[] = [];
  private bodyEq: BiquadFilterNode | null = null;
  private nasalPeakEq: BiquadFilterNode | null = null;
  private nasalLowEq: BiquadFilterNode | null = null;
  private presenceEq: BiquadFilterNode | null = null;
  private airEq: BiquadFilterNode | null = null;
  private masterGain: GainNode | null = null;

  private sourceNode: AudioBufferSourceNode | MediaStreamAudioSourceNode | null = null;
  private micStream: MediaStream | null = null;
  private currentBuffer: AudioBuffer | null = null;
  private stemCache = new Map<BuiltInStemId, Float32Array>();
  private uploadedPcm: Float32Array | null = null;
  private uploadedSr = 48000;
  private uploadedName = '';

  private autoTune = new AutoTuneEngine();
  private params: StudioParams;
  private isPlaying = false;
  private activeSource: InputSourceKind = 'baritone';

  // Live voice profile accumulators
  private f0History: number[] = [];
  private f1Acc = 0;
  private f2Acc = 0;
  private f3Acc = 0;
  private formCount = 0;
  private centroidAcc = 0;
  private voicedSec = 0;
  private prevF0 = 0;
  private lastGrDb = 0;
  private lastLufsDb = 0;
  private lastWorkletCons = 0;

  private waveRing = new Array<number>(64).fill(0);
  private waveWrite = 0;
  private pitchTrace: Array<{ inputHz: number; outputHz: number; voiced: boolean }> = [];
  private dryTimeBuf = new Float32Array(2048);
  private wetTimeBuf = new Float32Array(2048);

  constructor(initialParams: StudioParams) {
    this.params = { ...initialParams };
  }

  getActiveSource(): InputSourceKind {
    return this.activeSource;
  }

  getUploadedFileName(): string {
    return this.uploadedName;
  }

  getIsPlaying(): boolean {
    return this.isPlaying;
  }

  private async ensureContext(): Promise<AudioContext> {
    if (this.ctx) {
      if (this.ctx.state === 'suspended') await this.ctx.resume();
      return this.ctx;
    }

    const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    const ctx = new AudioCtx({ sampleRate: 48000 });
    this.ctx = ctx;

    this.dryAnalyser = ctx.createAnalyser();
    this.dryAnalyser.fftSize = 2048;
    this.dryAnalyser.smoothingTimeConstant = 0.65;

    this.wetAnalyser = ctx.createAnalyser();
    this.wetAnalyser.fftSize = 2048;
    this.wetAnalyser.smoothingTimeConstant = 0.65;

    // Anatomical 3-band formant resonator sculpt bank (F1/F2/F3 vocal tract coloration)
    this.formantFilters = [520, 1450, 2600].map((freq) => {
      const f = ctx.createBiquadFilter();
      f.type = 'peaking';
      f.frequency.value = freq;
      f.Q.value = 2.4;
      f.gain.value = 0;
      return f;
    });

    // Studio 4-Band Vocal EQ (Body, Nasal Peak + Cut, Presence, Air)
    this.bodyEq = ctx.createBiquadFilter();
    this.bodyEq.type = 'lowshelf';
    this.bodyEq.frequency.value = 170;

    this.nasalPeakEq = ctx.createBiquadFilter();
    this.nasalPeakEq.type = 'peaking';
    this.nasalPeakEq.frequency.value = 1150;
    this.nasalPeakEq.Q.value = 2.2;

    this.nasalLowEq = ctx.createBiquadFilter();
    this.nasalLowEq.type = 'peaking';
    this.nasalLowEq.frequency.value = 300;
    this.nasalLowEq.Q.value = 1.0;

    this.presenceEq = ctx.createBiquadFilter();
    this.presenceEq.type = 'peaking';
    this.presenceEq.frequency.value = 3200;
    this.presenceEq.Q.value = 0.9;

    this.airEq = ctx.createBiquadFilter();
    this.airEq.type = 'highshelf';
    this.airEq.frequency.value = 8800;

    this.masterGain = ctx.createGain();
    this.masterGain.gain.value = 0.95;

    // Load AudioWorklet via Blob URL for zero external bundler dependencies
    try {
      const blob = new Blob([WORKLET_CODE], { type: 'application/javascript' });
      const url = URL.createObjectURL(blob);
      await ctx.audioWorklet.addModule(url);
      URL.revokeObjectURL(url);
      const worklet = new AudioWorkletNode(ctx, 'alptugdub-worklet', {
        numberOfInputs: 1,
        numberOfOutputs: 1,
        outputChannelCount: [2],
      });
      worklet.port.onmessage = (ev) => {
        if (ev.data?.type === 'metrics') {
          this.lastGrDb = ev.data.grDb;
          this.lastLufsDb = ev.data.loudDb;
          this.lastWorkletCons = ev.data.cons;
        }
      };
      this.workletNode = worklet;
    } catch {
      this.workletNode = null;
    }

    // Wire DSP Chain: dryAnalyser -> [worklet] -> F1 -> F2 -> F3 -> body -> nasal -> presence -> air -> wetAnalyser -> master -> destination
    let tail: AudioNode = this.dryAnalyser;
    if (this.workletNode) {
      tail.connect(this.workletNode);
      tail = this.workletNode;
    }
    for (const ff of this.formantFilters) {
      tail.connect(ff);
      tail = ff;
    }
    tail.connect(this.bodyEq);
    this.bodyEq.connect(this.nasalPeakEq);
    this.nasalPeakEq.connect(this.nasalLowEq);
    this.nasalLowEq.connect(this.presenceEq);
    this.presenceEq.connect(this.airEq);
    this.airEq.connect(this.wetAnalyser);
    this.wetAnalyser.connect(this.masterGain);
    this.masterGain.connect(ctx.destination);

    this.syncParamsToNodes(0);
    return ctx;
  }

  updateParams(next: StudioParams): void {
    this.params = { ...next };
    this.syncParamsToNodes(0);
  }

  private syncParamsToNodes(tuneSt: number): void {
    const p = this.params;
    const age = ageShape(p.age);
    const now = this.ctx?.currentTime ?? 0;

    if (this.workletNode) {
      this.workletNode.port.postMessage({
        type: 'params',
        payload: {
          pitch: p.pitch + age.pitch,
          formant: p.formant + age.formant,
          regen: p.regen,
          glottal: p.glottal,
          growl: p.growl,
          breath: p.breath,
          age: p.age,
          mix: p.mix,
          outputDb: p.output,
          hpfHz: p.hpfHz,
          gateDb: p.gateDb,
          deEsser: p.deEsser,
          consonantGuard: p.consonantGuard,
          loudness: p.loudness,
          bypass: p.bypass,
          liveF0: this.prevF0 > 45 ? this.prevF0 : 140,
          tuneSt,
        },
      });
    }

    if (!this.ctx) return;

    const effFormant = p.bypass ? 0 : clamp(p.formant + age.formant, -8, 8);
    // Shape vocal tract resonances smoothly around anatomical F1, F2, F3
    const baseFreqs = [520, 1450, 2600];
    const weights = [1.15, 0.95, 0.78];
    this.formantFilters.forEach((filter, idx) => {
      const shiftedHz = clamp(
        baseFreqs[idx] * Math.pow(2, (effFormant * weights[idx]) / 12),
        180,
        4800
      );
      filter.frequency.setTargetAtTime(shiftedHz, now, 0.02);
      const emphasis = p.bypass ? 0 : clamp(Math.abs(effFormant) * 0.85, 0, 5.5) * (effFormant >= 0 ? 1 : 0.85);
      filter.gain.setTargetAtTime(emphasis * (idx === 0 && effFormant < 0 ? 1.15 : 0.85), now, 0.02);
    });

    const bodyAuto = p.autoTone ? clamp(-0.12 * p.pitch, -1.5, 2.5) : 0;
    const airAuto = age.air + (p.autoTone ? clamp(0.03 * p.pitch - 0.15 * effFormant, -1.5, 1.5) : 0);

    this.bodyEq?.gain.setTargetAtTime(p.bypass ? 0 : clamp(p.body + bodyAuto, -9, 9), now, 0.02);
    this.nasalPeakEq?.gain.setTargetAtTime(p.bypass ? 0 : 7.5 * p.nasal, now, 0.02);
    this.nasalLowEq?.gain.setTargetAtTime(p.bypass ? 0 : -3.8 * p.nasal, now, 0.02);
    this.presenceEq?.gain.setTargetAtTime(p.bypass ? 0 : clamp(p.presence, -6, 6), now, 0.02);
    this.airEq?.gain.setTargetAtTime(p.bypass ? 0 : clamp(p.air + airAuto, -9, 9), now, 0.02);
  }

  private getStemPcm(stemId: BuiltInStemId, sr: number): Float32Array {
    const cached = this.stemCache.get(stemId);
    if (cached) return cached;
    const pcm = synthesizeVocalStem(stemId, sr);
    this.stemCache.set(stemId, pcm);
    return pcm;
  }

  async selectSource(source: InputSourceKind, autoStart = false): Promise<void> {
    const wasPlaying = this.isPlaying;
    this.stop();
    this.activeSource = source;
    this.resetVoiceProfile();
    if (autoStart || wasPlaying) {
      await this.start();
    }
  }

  async loadAudioFile(file: File): Promise<void> {
    const ctx = await this.ensureContext();
    const arrayBuf = await file.arrayBuffer();
    const decoded = await ctx.decodeAudioData(arrayBuf);
    const ch0 = decoded.getChannelData(0);
    const mono = new Float32Array(decoded.length);
    if (decoded.numberOfChannels > 1) {
      const ch1 = decoded.getChannelData(1);
      for (let i = 0; i < decoded.length; i++) mono[i] = 0.5 * (ch0[i] + ch1[i]);
    } else {
      mono.set(ch0);
    }
    this.uploadedPcm = mono;
    this.uploadedSr = decoded.sampleRate;
    this.uploadedName = file.name;
    this.activeSource = 'file';
    this.resetVoiceProfile();
    await this.start();
  }

  async start(): Promise<void> {
    const ctx = await this.ensureContext();
    this.stopSourceOnly();

    if (this.activeSource === 'mic') {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false,
        },
      });
      this.micStream = stream;
      const micNode = ctx.createMediaStreamSource(stream);
      micNode.connect(this.dryAnalyser!);
      this.sourceNode = micNode;
      this.isPlaying = true;
      return;
    }

    let pcm: Float32Array;
    let sr = ctx.sampleRate;
    if (this.activeSource === 'file' && this.uploadedPcm) {
      pcm = this.uploadedPcm;
      sr = this.uploadedSr;
    } else {
      const stemId: BuiltInStemId =
        this.activeSource === 'mezzo' || this.activeSource === 'vocal_melody'
          ? this.activeSource
          : 'baritone';
      pcm = this.getStemPcm(stemId, ctx.sampleRate);
    }

    const audioBuf = ctx.createBuffer(1, pcm.length, sr);
    audioBuf.copyToChannel(new Float32Array(pcm), 0);
    this.currentBuffer = audioBuf;

    const bufSrc = ctx.createBufferSource();
    bufSrc.buffer = audioBuf;
    bufSrc.loop = true;
    bufSrc.connect(this.dryAnalyser!);
    bufSrc.start();
    this.sourceNode = bufSrc;
    this.isPlaying = true;
  }

  stop(): void {
    this.stopSourceOnly();
    this.isPlaying = false;
  }

  private stopSourceOnly(): void {
    if (this.sourceNode) {
      try {
        if ('stop' in this.sourceNode && typeof this.sourceNode.stop === 'function') {
          this.sourceNode.stop();
        }
        this.sourceNode.disconnect();
      } catch {
        // Ignore already-stopped AudioBufferSourceNode
      }
      this.sourceNode = null;
    }
    if (this.micStream) {
      this.micStream.getTracks().forEach((t) => t.stop());
      this.micStream = null;
    }
  }

  resetVoiceProfile(): void {
    this.f0History = [];
    this.f1Acc = 0;
    this.f2Acc = 0;
    this.f3Acc = 0;
    this.formCount = 0;
    this.centroidAcc = 0;
    this.voicedSec = 0;
    this.prevF0 = 0;
    this.autoTune.resetProfile();
  }

  exportProcessedWav(): { blob: Blob; filename: string } {
    const sr = this.ctx?.sampleRate ?? 48000;
    let srcPcm: Float32Array;
    let srcSr = sr;
    let baseName = 'alptugdub-render';

    if (this.activeSource === 'file' && this.uploadedPcm) {
      srcPcm = this.uploadedPcm;
      srcSr = this.uploadedSr;
      baseName = this.uploadedName.replace(/\.[^.]+$/, '') || 'upload';
    } else {
      const stemId: BuiltInStemId =
        this.activeSource === 'mezzo' || this.activeSource === 'vocal_melody'
          ? this.activeSource
          : 'baritone';
      srcPcm = this.getStemPcm(stemId, sr);
      baseName = stemId;
    }

    const rendered = processVocalBufferOffline(srcPcm, srcSr, this.params);
    const wavBytes = encodeWav16Bit(rendered.output, srcSr);
    const blob = new Blob([wavBytes.buffer as ArrayBuffer], { type: 'audio/wav' });
    return { blob, filename: `AlptugDUB-3.0-${baseName}.wav` };
  }

  getVoiceSummary(): VoiceSummary {
    if (this.f0History.length === 0) {
      return {
        medianF0: 0,
        lowF0: 0,
        highF0: 0,
        formants: [0, 0, 0],
        brightness: 0,
        breathiness: 0,
        seconds: this.voicedSec,
        type: 'Unknown',
        ready: false,
      };
    }
    const sorted = [...this.f0History].sort((a, b) => a - b);
    const medianF0 = sorted[Math.floor(sorted.length * 0.5)];
    const lowF0 = sorted[Math.floor(sorted.length * 0.1)];
    const highF0 = sorted[Math.floor(sorted.length * 0.9)];
    const formants: [number, number, number] =
      this.formCount > 0
        ? [this.f1Acc / this.formCount, this.f2Acc / this.formCount, this.f3Acc / this.formCount]
        : [0, 0, 0];

    return {
      medianF0,
      lowF0,
      highF0,
      formants,
      brightness: this.formCount > 0 ? this.centroidAcc / this.formCount : 0,
      breathiness: 0.15,
      seconds: this.voicedSec,
      type: classifyVoice(medianF0, formants),
      ready: this.voicedSec >= 0.15 && medianF0 > 40,
    };
  }

  /** Polls real-time audio buffers at ~30 Hz to update telemetry, pitch tracking, formants, and Auto-Tune. */
  pollTelemetry(dtSec = 0.033): AudioTelemetry {
    const summary = this.getVoiceSummary();
    if (!this.isPlaying || !this.dryAnalyser || !this.wetAnalyser || !this.ctx) {
      return {
        inputLevel: 0,
        outputLevel: 0,
        inputDb: -60,
        outputDb: -60,
        limiterGrDb: 0,
        lufsDeltaDb: 0,
        liveF0: 0,
        targetF0: 0,
        confidence: 0,
        voiced: 0,
        consonant: 0,
        regenActive: 0,
        detectedKey: 0,
        detectedScale: 'chromatic',
        tuneCents: 0,
        waveHistory: [...this.waveRing],
        drySpectrum: new Array<number>(64).fill(-72),
        wetSpectrum: new Array<number>(64).fill(-72),
        pitchTrace: this.pitchTrace,
        summary,
      };
    }

    const sr = this.ctx.sampleRate;
    this.dryAnalyser.getFloatTimeDomainData(this.dryTimeBuf);
    this.wetAnalyser.getFloatTimeDomainData(this.wetTimeBuf);

    let inPeak = 0;
    let outPeak = 0;
    let signedSum = 0;
    for (let i = 0; i < this.dryTimeBuf.length; i++) {
      inPeak = Math.max(inPeak, Math.abs(this.dryTimeBuf[i]));
      const w = this.wetTimeBuf[i];
      outPeak = Math.max(outPeak, Math.abs(w));
      signedSum += w;
    }

    const yin = detectPitchYin(this.dryTimeBuf, sr, {
      hpfHz: this.params.hpfHz,
      prevF0: this.prevF0,
    });
    const cons = analyzeConsonantGuard(this.dryTimeBuf.subarray(0, 1024), sr, yin.confidence);

    const cepDry = extractTrueCepstralEnvelope(this.dryTimeBuf.subarray(0, 1024), sr, yin.f0);
    const cepWet = extractTrueCepstralEnvelope(this.wetTimeBuf.subarray(0, 1024), sr, yin.f0);

    if (yin.voiced && inPeak > 0.006) {
      this.prevF0 = yin.f0;
      this.f0History.push(yin.f0);
      if (this.f0History.length > 240) this.f0History.shift();
      this.voicedSec += dtSec;

      this.f1Acc += cepDry.formants[0];
      this.f2Acc += cepDry.formants[1];
      this.f3Acc += cepDry.formants[2];
      this.centroidAcc += cepDry.centroidHz;
      this.formCount++;
    }

    const updatedSummary = this.getVoiceSummary();
    const age = ageShape(this.params.age);
    const tune = this.autoTune.update({
      f0In: yin.f0,
      confidence: yin.confidence,
      pitchShiftSt: this.params.pitch + age.pitch,
      amount: this.params.autotune,
      retuneSpeedMs: this.params.retuneSpeedMs,
      vibratoPreserve: this.params.vibratoPreserve,
      scaleMode: this.params.scaleMode,
      manualKey: this.params.manualKey,
      voiceType: updatedSummary.type,
      dtSec,
    });

    // Push live Auto-Tune delta & tracked F0 into the AudioWorklet for sample-accurate glottal lock
    this.syncParamsToNodes(tune.correctionSemitones);

    const effShiftSt = this.params.bypass ? 0 : this.params.pitch + age.pitch + tune.correctionSemitones;
    const targetF0 = yin.voiced ? yin.f0 * Math.pow(2, effShiftSt / 12) : 0;

    const signedAvg = Math.tanh(3.5 * (signedSum / this.wetTimeBuf.length));
    const waveSample = outPeak > 0.002 ? (signedAvg >= 0 ? outPeak : -outPeak) : 0;
    this.waveRing[this.waveWrite & 63] = waveSample;
    this.waveWrite++;

    this.pitchTrace.push({
      inputHz: yin.voiced ? yin.f0 : 0,
      outputHz: targetF0,
      voiced: yin.voiced,
    });
    if (this.pitchTrace.length > 96) this.pitchTrace.shift();

    // Ordered wave ring for smooth left-to-right display
    const orderedWave = new Array<number>(64);
    for (let i = 0; i < 64; i++) {
      orderedWave[i] = this.waveRing[(this.waveWrite + i) & 63];
    }

    const consonantActivity = Math.max(cons.consonantProb, this.lastWorkletCons);
    const voicedProb = yin.voiced ? clamp(yin.confidence * (1 - consonantActivity * 0.65), 0, 1) : 0;

    return {
      inputLevel: clamp(inPeak, 0, 1),
      outputLevel: clamp(outPeak, 0, 1),
      inputDb: 20 * Math.log10(Math.max(1e-3, inPeak)),
      outputDb: 20 * Math.log10(Math.max(1e-3, outPeak)),
      limiterGrDb: this.lastGrDb,
      lufsDeltaDb: this.lastLufsDb,
      liveF0: yin.voiced ? yin.f0 : 0,
      targetF0,
      confidence: yin.confidence,
      voiced: voicedProb,
      consonant: consonantActivity,
      regenActive: this.params.regen * voicedProb,
      detectedKey: tune.key,
      detectedScale: tune.scale,
      tuneCents: tune.correctionCents,
      waveHistory: orderedWave,
      drySpectrum: spectrumToLogBins(cepDry.magnitudes, sr, 64),
      wetSpectrum: spectrumToLogBins(cepWet.magnitudes, sr, 64),
      pitchTrace: [...this.pitchTrace],
      summary: updatedSummary,
    };
  }

  dispose(): void {
    this.stop();
    if (this.ctx) {
      this.ctx.close().catch(() => {});
      this.ctx = null;
    }
  }
}
