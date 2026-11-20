import React, { useEffect, useRef } from 'react';
import { ageShape, clamp } from '../dsp/characters';
import { warpFormantFreq } from '../dsp/cepstralFormant';
import type { AudioTelemetry, StudioParams } from '../dsp/types';

interface SpectrumAnalyzerViewProps {
  telemetry: AudioTelemetry;
  params: StudioParams;
  isPlaying: boolean;
  lang: 'tr' | 'en';
}

const LOG_TICKS_HZ = [100, 250, 500, 1000, 2000, 4000, 8000, 14000];

function hzToX(hz: number, width: number, padLeft: number, padRight: number): number {
  const minLog = Math.log(60);
  const maxLog = Math.log(16000);
  const u = clamp((Math.log(clamp(hz, 60, 16000)) - minLog) / (maxLog - minLog), 0, 1);
  return padLeft + u * (width - padLeft - padRight);
}

export const SpectrumAnalyzerView: React.FC<SpectrumAnalyzerViewProps> = ({
  telemetry,
  params,
  isPlaying,
  lang,
}) => {
  const specCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const pitchCanvasRef = useRef<HTMLCanvasElement | null>(null);

  const age = ageShape(params.age);
  const effFormant = params.bypass ? 0 : clamp(params.formant + age.formant, -8, 8);
  const rawFormants =
    telemetry.summary.formants[0] > 0 ? telemetry.summary.formants : ([520, 1450, 2550] as [number, number, number]);
  const warpedFormants: [number, number, number] = [
    warpFormantFreq(rawFormants[0], effFormant),
    warpFormantFreq(rawFormants[1], effFormant),
    warpFormantFreq(rawFormants[2], effFormant),
  ];

  useEffect(() => {
    const canvas = specCanvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    const w = Math.max(320, Math.round(rect.width));
    const h = Math.max(200, Math.round(rect.height));
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
    }

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    const padL = 42;
    const padR = 16;
    const padT = 18;
    const padB = 26;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;

    // Grid lines (dB horizontal)
    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.lineWidth = 1;
    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillStyle = '#64748B';
    for (const db of [-12, -24, -36, -48, -60, -72]) {
      const y = padT + ((-db) / 78) * plotH;
      ctx.beginPath();
      ctx.moveTo(padL, y);
      ctx.lineTo(w - padR, y);
      ctx.stroke();
      ctx.fillText(`${db}`, 6, y + 3);
    }

    // Frequency vertical grid
    for (const hz of LOG_TICKS_HZ) {
      const x = hzToX(hz, w, padL, padR);
      ctx.beginPath();
      ctx.moveTo(x, padT);
      ctx.lineTo(x, h - padB);
      ctx.stroke();
      const label = hz >= 1000 ? `${hz / 1000}k` : `${hz}`;
      ctx.fillText(label, x - 10, h - 8);
    }

    // Draw HPF cutoff line
    const hpfX = hzToX(params.hpfHz, w, padL, padR);
    ctx.save();
    ctx.strokeStyle = 'rgba(244, 63, 94, 0.55)';
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(hpfX, padT);
    ctx.lineTo(hpfX, h - padB);
    ctx.stroke();
    ctx.fillStyle = '#FB7185';
    ctx.fillText(`HPF ${Math.round(params.hpfHz)}Hz`, hpfX + 4, padT + 11);
    ctx.restore();

    const drawCurve = (bins: number[], stroke: string, fill: string, dashed = false) => {
      if (bins.length < 2) return;
      ctx.save();
      if (dashed) ctx.setLineDash([4, 3]);
      ctx.beginPath();
      for (let i = 0; i < bins.length; i++) {
        const x = padL + (i / (bins.length - 1)) * plotW;
        const db = clamp(bins[i], -78, 0);
        const y = padT + ((-db) / 78) * plotH;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = stroke;
      ctx.lineWidth = dashed ? 1.4 : 2.1;
      ctx.stroke();

      if (!dashed) {
        ctx.lineTo(padL + plotW, h - padB);
        ctx.lineTo(padL, h - padB);
        ctx.closePath();
        ctx.fillStyle = fill;
        ctx.fill();
      }
      ctx.restore();
    };

    drawCurve(telemetry.drySpectrum, 'rgba(148, 163, 184, 0.65)', 'transparent', true);
    drawCurve(telemetry.wetSpectrum, '#22D3EE', 'rgba(34, 211, 238, 0.12)', false);

    // Overlay F1 / F2 / F3 anatomical formant shift markers
    const names = ['F1', 'F2', 'F3'];
    for (let i = 0; i < 3; i++) {
      const xDry = hzToX(rawFormants[i], w, padL, padR);
      const xWet = hzToX(warpedFormants[i], w, padL, padR);
      const yTop = padT + 26 + i * 18;

      ctx.strokeStyle = 'rgba(245, 158, 11, 0.45)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(xWet, yTop);
      ctx.lineTo(xWet, h - padB);
      ctx.stroke();

      // Shift arrow from dry formant to warped formant
      if (Math.abs(xWet - xDry) > 3) {
        ctx.strokeStyle = '#F59E0B';
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        ctx.moveTo(xDry, yTop);
        ctx.lineTo(xWet, yTop);
        ctx.stroke();
      }

      ctx.fillStyle = '#FBBF24';
      ctx.beginPath();
      ctx.arc(xWet, yTop, 3.5, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillText(`${names[i]} ${Math.round(warpedFormants[i])}Hz`, clamp(xWet + 6, padL, w - 74), yTop + 3);
    }

    ctx.restore();
  }, [telemetry.drySpectrum, telemetry.wetSpectrum, rawFormants, warpedFormants, params.hpfHz]);

  useEffect(() => {
    const canvas = pitchCanvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    const w = Math.max(300, Math.round(rect.width));
    const h = Math.max(150, Math.round(rect.height));
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
    }

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    const padL = 42;
    const padR = 14;
    const padT = 14;
    const padB = 20;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;

    const minLog = Math.log(55);
    const maxLog = Math.log(560);
    const fToY = (hz: number) => {
      const u = clamp((Math.log(clamp(hz, 55, 560)) - minLog) / (maxLog - minLog), 0, 1);
      return padT + (1 - u) * plotH;
    };

    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillStyle = '#64748B';
    for (const hz of [65, 110, 165, 220, 330, 440]) {
      const y = fToY(hz);
      ctx.beginPath();
      ctx.moveTo(padL, y);
      ctx.lineTo(w - padR, y);
      ctx.stroke();
      ctx.fillText(`${hz}`, 8, y + 3);
    }

    const trace = telemetry.pitchTrace;
    if (trace.length > 1) {
      const drawTrace = (pick: (p: { inputHz: number; outputHz: number; voiced: boolean }) => number, color: string, width: number) => {
        ctx.strokeStyle = color;
        ctx.lineWidth = width;
        let drawing = false;
        ctx.beginPath();
        for (let i = 0; i < trace.length; i++) {
          const val = pick(trace[i]);
          const x = padL + (i / Math.max(1, trace.length - 1)) * plotW;
          if (trace[i].voiced && val > 45) {
            const y = fToY(val);
            if (!drawing) {
              ctx.moveTo(x, y);
              drawing = true;
            } else {
              ctx.lineTo(x, y);
            }
          } else {
            drawing = false;
          }
        }
        ctx.stroke();
      };

      drawTrace((p) => p.inputHz, 'rgba(148, 163, 184, 0.7)', 1.5);
      drawTrace((p) => p.outputHz, '#F59E0B', 2.4);
    }
    ctx.restore();
  }, [telemetry.pitchTrace]);

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-1 gap-4 lg:grid-cols-12">
        {/* Main Log-Frequency Spectrum & Formant Shift Canvas */}
        <section className="lg:col-span-8 rounded-xl border border-white/10 bg-[#0B0F17] p-4">
          <div className="flex flex-wrap items-center justify-between gap-2 pb-2">
            <div>
              <h2 className="text-sm font-bold text-slate-100">
                {lang === 'tr'
                  ? 'Gerçek Zamanlı Spektrum ve Doğrusal Olmayan Formant Haritası'
                  : 'Real-Time Spectrum & Non-Linear Formant Map'}
              </h2>
              <p className="text-xs text-slate-400">
                {lang === 'tr'
                  ? 'Kesikli gri: Giriş (Dry) · Mavi: İşlenmiş (Wet) · Kehribar oklar: F1/F2/F3 anatomik bükme'
                  : 'Dashed gray: Dry input · Cyan: Processed wet · Amber vectors: F1/F2/F3 anatomical warp'}
              </p>
            </div>
            <span className="font-mono text-xs tabular-nums text-slate-400">
              {isPlaying
                ? lang === 'tr'
                  ? '2048-pt Sıfır Dolgulu FFT'
                  : '2048-pt Zero-Padded FFT'
                : lang === 'tr'
                  ? 'Canlı veri için sesi oynatın'
                  : 'Play audio for live spectrum'}
            </span>
          </div>
          <div className="h-[250px] w-full">
            <canvas
              ref={specCanvasRef}
              data-testid="spectrum-canvas"
              className="h-full w-full block"
            />
          </div>
        </section>

        {/* Precision Formant & Engine Telemetry Table */}
        <section className="lg:col-span-4 rounded-xl border border-white/10 bg-[#0B0F17] p-4 flex flex-col justify-between">
          <div>
            <h2 className="text-sm font-bold text-slate-100">
              {lang === 'tr' ? 'Anatomik Formant ve Sinyal Ölçümü' : 'Anatomical Formant & Signal Metrics'}
            </h2>
            <p className="mt-0.5 text-xs text-slate-400">
              {lang === 'tr'
                ? 'F0-uyarlamalı gerçek zarf (True-Envelope) kepstral çıkarımı.'
                : 'F0-adaptive true-envelope cepstral extraction.'}
            </p>

            <table className="mt-4 w-full text-xs">
              <thead>
                <tr className="border-b border-white/10 text-left text-slate-500">
                  <th className="pb-2 font-medium">{lang === 'tr' ? 'Bant' : 'Band'}</th>
                  <th className="pb-2 font-medium text-right">{lang === 'tr' ? 'Kaynak' : 'Source'}</th>
                  <th className="pb-2 font-medium text-right">{lang === 'tr' ? 'Hedef' : 'Warped'}</th>
                  <th className="pb-2 font-medium text-right">Δ st</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-white/5 font-mono tabular-nums">
                {(['F1 (Yutak)', 'F2 (Dil)', 'F3 (Dudak)'] as const).map((label, idx) => {
                  const src = rawFormants[idx];
                  const dst = warpedFormants[idx];
                  const deltaSt = src > 10 ? 12 * Math.log2(dst / src) : 0;
                  return (
                    <tr key={label}>
                      <td className="py-2 font-sans text-slate-300">{label}</td>
                      <td className="py-2 text-right text-slate-400">{Math.round(src)} Hz</td>
                      <td className="py-2 text-right text-amber-300">{Math.round(dst)} Hz</td>
                      <td className="py-2 text-right text-cyan-300">
                        {deltaSt > 0 ? '+' : ''}
                        {deltaSt.toFixed(1)}
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>

          <div className="mt-4 grid grid-cols-2 gap-3 border-t border-white/10 pt-3 font-mono tabular-nums text-xs">
            <div>
              <div className="font-sans text-[11px] text-slate-500">
                {lang === 'tr' ? 'Ünsüz Koruma' : 'Consonant Guard'}
              </div>
              <div className="mt-0.5 text-sm font-semibold text-cyan-300">
                {Math.round(telemetry.consonant * 100)}%
              </div>
            </div>
            <div>
              <div className="font-sans text-[11px] text-slate-500">
                {lang === 'tr' ? 'YIN Perde Güveni' : 'YIN Confidence'}
              </div>
              <div className="mt-0.5 text-sm font-semibold text-amber-300">
                {Math.round(telemetry.confidence * 100)}%
              </div>
            </div>
            <div>
              <div className="font-sans text-[11px] text-slate-500">
                {lang === 'tr' ? 'LUFS Telafisi' : 'LUFS Match'}
              </div>
              <div className="mt-0.5 text-sm font-semibold text-slate-200">
                {telemetry.lufsDeltaDb > 0 ? '+' : ''}
                {telemetry.lufsDeltaDb.toFixed(1)} dB
              </div>
            </div>
            <div>
              <div className="font-sans text-[11px] text-slate-500">Limiter GR</div>
              <div className="mt-0.5 text-sm font-semibold text-rose-400">
                {telemetry.limiterGrDb.toFixed(1)} dB
              </div>
            </div>
          </div>
        </section>
      </div>

      {/* Pitch Tracking & Auto-Tune Trajectory */}
      <section className="rounded-xl border border-white/10 bg-[#0B0F17] p-4">
        <div className="flex flex-wrap items-center justify-between gap-2 pb-2">
          <div>
            <h2 className="text-sm font-bold text-slate-100">
              {lang === 'tr' ? 'Perde İzleme ve Auto-Tune Eğrisi (55–560 Hz)' : 'Pitch Tracking & Auto-Tune Trajectory (55–560 Hz)'}
            </h2>
            <p className="text-xs text-slate-400">
              {lang === 'tr'
                ? 'Gri eğri: Giriş F0 · Kehribar eğri: Perde + Auto-Tune çıkış F0'
                : 'Gray trace: Input F0 · Amber trace: Pitch + Auto-Tune output F0'}
            </p>
          </div>
          <span className="font-mono text-xs tabular-nums text-amber-300">
            {telemetry.liveF0 > 40
              ? `${Math.round(telemetry.liveF0)} Hz → ${Math.round(telemetry.targetF0)} Hz`
              : '—'}
          </span>
        </div>
        <div className="h-[165px] w-full">
          <canvas ref={pitchCanvasRef} data-testid="pitch-canvas" className="h-full w-full block" />
        </div>
      </section>
    </div>
  );
};
