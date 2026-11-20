import React, { useEffect, useRef } from 'react';
import { ageShape, clamp, formatKeyScale, formatPitchNote } from '../dsp/characters';
import { warpFormantFreq } from '../dsp/cepstralFormant';
import type { AudioTelemetry, StudioParams } from '../dsp/types';

interface OscilloscopeLensProps {
  telemetry: AudioTelemetry;
  params: StudioParams;
  isPlaying: boolean;
  lang: 'tr' | 'en';
}

function levelToPercent(linear: number): number {
  const db = 20 * Math.log10(Math.max(1e-3, linear));
  return clamp(((db + 60) / 60) * 100, 0, 100);
}

export const OscilloscopeLens: React.FC<OscilloscopeLensProps> = ({
  telemetry,
  params,
  isPlaying,
  lang,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const phaseRef = useRef(0);
  const ampRef = useRef(0);
  const peakInRef = useRef(0);
  const peakOutRef = useRef(0);
  const telemetryRef = useRef(telemetry);
  const paramsRef = useRef(params);
  const playingRef = useRef(isPlaying);

  telemetryRef.current = telemetry;
  paramsRef.current = params;
  playingRef.current = isPlaying;

  useEffect(() => {
    let rafId = 0;
    const render = () => {
      const canvas = canvasRef.current;
      if (canvas) {
        const ctx = canvas.getContext('2d');
        const dpr = window.devicePixelRatio || 1;
        const rect = canvas.getBoundingClientRect();
        const w = Math.max(300, Math.round(rect.width));
        const h = Math.max(120, Math.round(rect.height));
        if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
          canvas.width = Math.round(w * dpr);
          canvas.height = Math.round(h * dpr);
        }

        if (ctx) {
          ctx.save();
          ctx.scale(dpr, dpr);
          ctx.clearRect(0, 0, w, h);

          const tel = telemetryRef.current;
          const p = paramsRef.current;
          const active = playingRef.current;

          const targetAmp = active
            ? clamp(Math.max(tel.outputLevel * 1.25, tel.inputLevel * 0.9, 0.14), 0, 1)
            : 0.08;
          ampRef.current += (targetAmp - ampRef.current) * (targetAmp > ampRef.current ? 0.32 : 0.12);
          phaseRef.current +=
            (active ? 0.085 : 0.025) +
            0.16 * ampRef.current +
            (tel.liveF0 > 50 ? 0.025 * (tel.liveF0 / 200) : 0);

          const cy = h * 0.52;

          // Subtle oscilloscope calibration grid
          ctx.strokeStyle = 'rgba(255,255,255,0.035)';
          ctx.lineWidth = 1;
          for (let i = 1; i <= 5; i++) {
            const x = (w * i) / 6;
            ctx.beginPath();
            ctx.moveTo(x, 12);
            ctx.lineTo(x, h - 12);
            ctx.stroke();
          }
          ctx.beginPath();
          ctx.moveTo(20, cy);
          ctx.lineTo(w - 20, cy);
          ctx.stroke();

          // Warm subtle back-illumination proportional to voice energy
          const bgGrad = ctx.createRadialGradient(w * 0.5, cy, 8, w * 0.5, cy, w * 0.42);
          bgGrad.addColorStop(0, `rgba(245, 158, 11, ${0.07 + 0.1 * ampRef.current})`);
          bgGrad.addColorStop(0.55, `rgba(34, 211, 238, ${0.03 + 0.05 * ampRef.current})`);
          bgGrad.addColorStop(1, 'rgba(0, 0, 0, 0)');
          ctx.fillStyle = bgGrad;
          ctx.fillRect(0, 0, w, h);

          // Multi-ribbon voice-reactive harmonic traces
          const pitchNorm = clamp((tel.liveF0 > 45 ? tel.liveF0 : 140) / 140, 0.65, 2.1);
          const harmFactor = clamp(1 + 0.025 * p.pitch + 0.035 * p.formant, 0.72, 1.85);
          const baseAmp = 8 + (h * 0.34) * ampRef.current;

          const ribbons = [
            { freq: 2.3 * harmFactor, phaseMul: 0.0, speed: 1.0, ampScale: 0.92, width: 2.2, color: '#FBBF24' },
            { freq: 3.2 * harmFactor, phaseMul: 1.25, speed: 1.22, ampScale: 1.0, width: 1.8, color: '#F59E0B' },
            { freq: 4.1 * harmFactor, phaseMul: 2.45, speed: 0.86, ampScale: 0.76, width: 1.5, color: '#22D3EE' },
            { freq: 1.65 * harmFactor, phaseMul: 3.8, speed: 0.64, ampScale: 0.56, width: 1.2, color: '#94A3B8' },
          ];

          const leftX = 28;
          const spanW = w - 56;
          const pts = 140;
          const ring = tel.waveHistory;

          for (let r = ribbons.length - 1; r >= 0; r--) {
            const rb = ribbons[r];
            ctx.beginPath();
            for (let i = 0; i <= pts; i++) {
              const u = i / pts;
              const win = Math.pow(Math.sin(Math.PI * u), 1.85);
              const idx = clamp(Math.floor(u * 63), 0, 63);
              const envVal = Math.abs(ring[idx] ?? 0);
              const envMod = active ? 0.38 + 0.95 * clamp(envVal * 1.45, 0, 1) : 0.42;

              const ph = phaseRef.current * rb.speed + rb.phaseMul;
              const carrier = Math.sin(u * Math.PI * 2 * rb.freq * (0.85 + 0.16 * pitchNorm) + ph);
              const mod =
                0.72 * carrier +
                0.28 * Math.sin(u * Math.PI * 2 * (rb.freq * 1.75) - ph * 1.25);
              const sibRipple = tel.consonant * 0.16 * Math.sin(u * 36 + ph * 2.6);
              const y = cy + win * envMod * baseAmp * rb.ampScale * (mod + sibRipple);
              const x = leftX + u * spanW;
              if (i === 0) ctx.moveTo(x, y);
              else ctx.lineTo(x, y);
            }

            // Outer soft phosphor glow
            ctx.strokeStyle = rb.color;
            ctx.globalAlpha = 0.16 + 0.12 * ampRef.current;
            ctx.lineWidth = rb.width * 3.2;
            ctx.stroke();

            // Crisp core line
            ctx.globalAlpha = r === 0 ? 0.95 : 0.82;
            ctx.lineWidth = rb.width;
            ctx.stroke();
          }
          ctx.restore();
        }
      }
      rafId = requestAnimationFrame(render);
    };
    rafId = requestAnimationFrame(render);
    return () => cancelAnimationFrame(rafId);
  }, []);

  // Peak-hold tracking
  const inPct = levelToPercent(telemetry.inputLevel);
  const outPct = levelToPercent(telemetry.outputLevel);
  peakInRef.current = Math.max(inPct, peakInRef.current - 0.65);
  peakOutRef.current = Math.max(outPct, peakOutRef.current - 0.65);

  // Exact warped formants matching the DSP engine (fixes v2.0 double-offset follow bug)
  const age = ageShape(params.age);
  const effFormant = params.bypass ? 0 : clamp(params.formant + age.formant, -8, 8);
  const hasFormants = telemetry.summary.formants[0] > 0;
  const f1 = hasFormants ? Math.round(warpFormantFreq(telemetry.summary.formants[0], effFormant)) : 0;
  const f2 = hasFormants ? Math.round(warpFormantFreq(telemetry.summary.formants[1], effFormant)) : 0;
  const f3 = hasFormants ? Math.round(warpFormantFreq(telemetry.summary.formants[2], effFormant)) : 0;

  let engineStateLabel = lang === 'tr' ? 'Hazır' : 'Ready';
  let engineStateColor = 'text-slate-400';
  if (params.bypass) {
    engineStateLabel = 'Bypass';
    engineStateColor = 'text-slate-400';
  } else if (isPlaying && params.autotune > 0.01) {
    engineStateLabel = `Auto-Tune: ${formatKeyScale(
      params.scaleMode === 'auto' ? telemetry.detectedKey : params.manualKey,
      params.scaleMode === 'auto' ? telemetry.detectedScale : params.scaleMode,
      lang
    )}`;
    engineStateColor = 'text-amber-300';
  } else if (isPlaying && telemetry.consonant > 0.4 && telemetry.voiced < 0.45) {
    engineStateLabel = lang === 'tr' ? 'Ünsüz Koruma' : 'Consonant Guard';
    engineStateColor = 'text-cyan-300';
  } else if (isPlaying && telemetry.voiced > 0.35) {
    engineStateLabel = lang === 'tr' ? 'LF Sentez' : 'LF Synth';
    engineStateColor = 'text-amber-300';
  }

  const grAbs = Math.abs(telemetry.limiterGrDb);
  const grPct = clamp((grAbs / 12) * 100, 0, 100);

  return (
    <section
      aria-label={lang === 'tr' ? 'Canlı Ses Osiloskobu ve Ölçüm Ekranı' : 'Live Voice Oscilloscope & Metering'}
      className="relative overflow-hidden rounded-xl border border-white/10 bg-[#070A10] px-4 py-3 sm:px-5"
    >
      {/* Top HUD inside glass oscilloscope */}
      <div className="flex flex-wrap items-center justify-between gap-2 pb-1">
        <div className="flex items-baseline gap-2.5 font-mono tabular-nums">
          <span className="text-base sm:text-lg font-semibold text-slate-100">
            {telemetry.liveF0 > 40 ? `${Math.round(telemetry.liveF0)} Hz` : '— Hz'}
          </span>
          <span className="text-xs text-slate-500">·</span>
          <span className="text-sm font-medium text-amber-300">
            {telemetry.liveF0 > 40 ? formatPitchNote(telemetry.liveF0) : '—'}
          </span>
          {telemetry.targetF0 > 40 && !params.bypass && Math.abs(telemetry.targetF0 - telemetry.liveF0) > 1.5 && (
            <span className="text-xs text-cyan-300">
              → {Math.round(telemetry.targetF0)} Hz ({formatPitchNote(telemetry.targetF0)})
            </span>
          )}
          {params.autotune > 0.01 && Math.abs(telemetry.tuneCents) > 0.5 && (
            <span className="text-xs text-amber-400">
              {telemetry.tuneCents > 0 ? '+' : ''}
              {Math.round(telemetry.tuneCents)} ¢
            </span>
          )}
        </div>

        <div className="flex flex-wrap items-center gap-3 text-xs font-mono tabular-nums">
          <span className="text-slate-400">
            {hasFormants
              ? `F1 ${f1} · F2 ${f2} · F3 ${f3} Hz`
              : lang === 'tr'
                ? 'Formant analizi hazır'
                : 'Formant analyzer ready'}
          </span>
          <span className="text-slate-600" aria-hidden="true">
            |
          </span>
          <span className={`font-sans text-xs font-semibold ${engineStateColor}`}>
            {engineStateLabel}
          </span>
        </div>
      </div>

      {/* Voice-reactive multi-ribbon oscilloscope canvas */}
      <div className="relative h-[132px] sm:h-[152px] w-full">
        <canvas
          ref={canvasRef}
          data-testid="oscilloscope-canvas"
          className="h-full w-full block"
        />
      </div>

      {/* Bottom Calibrated Studio Metering Strip (In, Out, Limiter GR) */}
      <div className="mt-1 grid grid-cols-1 gap-2.5 sm:grid-cols-[1fr_1fr_170px] items-center pt-1 border-t border-white/5">
        <div className="flex items-center gap-2.5">
          <span className="w-10 text-[11px] font-medium text-slate-400 whitespace-nowrap">
            {lang === 'tr' ? 'Giriş' : 'Input'}
          </span>
          <div className="relative h-2 flex-1 overflow-hidden rounded-full bg-[#131924]">
            <div
              className="h-full rounded-full bg-gradient-to-r from-amber-500 via-amber-300 to-cyan-400 transition-[width] duration-75"
              style={{ width: `${inPct}%` }}
            />
            {peakInRef.current > 2 && (
              <div
                className="absolute top-0 bottom-0 w-0.5 bg-white/80"
                style={{ left: `${clamp(peakInRef.current, 0, 99)}%` }}
              />
            )}
          </div>
          <span className="w-14 text-right font-mono text-[11px] tabular-nums text-slate-400">
            {telemetry.inputLevel > 0.0012 ? `${telemetry.inputDb.toFixed(1)} dB` : '-∞ dB'}
          </span>
        </div>

        <div className="flex items-center gap-2.5">
          <span className="w-10 text-[11px] font-medium text-slate-400 whitespace-nowrap">
            {lang === 'tr' ? 'Çıkış' : 'Output'}
          </span>
          <div className="relative h-2 flex-1 overflow-hidden rounded-full bg-[#131924]">
            <div
              className="h-full rounded-full bg-gradient-to-r from-amber-500 via-emerald-300 to-cyan-400 transition-[width] duration-75"
              style={{ width: `${outPct}%` }}
            />
            {peakOutRef.current > 2 && (
              <div
                className="absolute top-0 bottom-0 w-0.5 bg-white/80"
                style={{ left: `${clamp(peakOutRef.current, 0, 99)}%` }}
              />
            )}
          </div>
          <span className="w-14 text-right font-mono text-[11px] tabular-nums text-slate-300">
            {telemetry.outputLevel > 0.0012 ? `${telemetry.outputDb.toFixed(1)} dB` : '-∞ dB'}
          </span>
        </div>

        <div className="flex items-center gap-2">
          <span className="text-[11px] font-medium text-slate-400 whitespace-nowrap">GR</span>
          <div className="relative h-2 flex-1 overflow-hidden rounded-full bg-[#131924]">
            <div
              className="ml-auto h-full rounded-full bg-rose-500 transition-[width] duration-75"
              style={{ width: `${grPct}%` }}
            />
          </div>
          <span className="w-12 text-right font-mono text-[11px] tabular-nums text-rose-400">
            {grAbs > 0.05 ? `${telemetry.limiterGrDb.toFixed(1)}` : '0.0'} dB
          </span>
        </div>
      </div>
    </section>
  );
};
