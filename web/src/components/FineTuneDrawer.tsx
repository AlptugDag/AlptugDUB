import React from 'react';
import { X } from 'lucide-react';
import { formatDb, formatPercent, NOTE_NAMES } from '../dsp/characters';
import type { ScaleMode, StudioParams } from '../dsp/types';

interface FineTuneDrawerProps {
  open: boolean;
  params: StudioParams;
  onChange: (patch: Partial<StudioParams>) => void;
  onClose: () => void;
  lang: 'tr' | 'en';
}

interface SliderRowProps {
  id: string;
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  display: string;
  onChange: (v: number) => void;
}

const SliderRow: React.FC<SliderRowProps> = ({
  id,
  label,
  value,
  min,
  max,
  step,
  display,
  onChange,
}) => (
  <div className="flex flex-col gap-1.5">
    <div className="flex items-center justify-between text-xs">
      <label htmlFor={id} className="font-medium text-slate-300">
        {label}
      </label>
      <span className="font-mono tabular-nums text-amber-300">{display}</span>
    </div>
    <input
      id={id}
      type="range"
      min={min}
      max={max}
      step={step}
      value={value}
      onChange={(e) => onChange(parseFloat(e.target.value))}
      className="studio-range w-full"
    />
  </div>
);

export const FineTuneDrawer: React.FC<FineTuneDrawerProps> = ({
  open,
  params,
  onChange,
  onClose,
  lang,
}) => {
  if (!open) return null;

  const scaleOptions: Array<{ value: ScaleMode; labelTr: string; labelEn: string }> = [
    { value: 'auto', labelTr: 'Otomatik Algıla', labelEn: 'Auto Detect' },
    { value: 'major', labelTr: 'Majör', labelEn: 'Major' },
    { value: 'minor', labelTr: 'Minör', labelEn: 'Minor' },
    { value: 'chromatic', labelTr: 'Kromatik', labelEn: 'Chromatic' },
    { value: 'hijaz', labelTr: 'Hicaz', labelEn: 'Hijaz' },
    { value: 'pentatonic', labelTr: 'Pentatonik', labelEn: 'Pentatonic' },
  ];

  const toggles: Array<{
    key: 'autoTract' | 'autoResponse' | 'autoSteady' | 'autoTone';
    labelTr: string;
    labelEn: string;
    descTr: string;
    descEn: string;
  }> = [
    {
      key: 'autoTract',
      labelTr: 'Doğal gırtlak hareketi',
      labelEn: 'Natural tract motion',
      descTr: 'Perde yükselip alçaldıkça gırtlak hafifçe esner (±2 st).',
      descEn: 'Vocal tract gently tracks pitch contour (±2 st).',
    },
    {
      key: 'autoResponse',
      labelTr: 'Dinamik tepki',
      labelEn: 'Dynamic response',
      descTr: 'Hırıltı ve netlik konuşma şiddetine göre nefes alır.',
      descEn: 'Growl and presence follow vocal effort.',
    },
    {
      key: 'autoSteady',
      labelTr: 'Titreşimi yumuşat',
      labelEn: 'Steady pitch',
      descTr: 'Ani perde dalgalanmalarını kontrol altına alır.',
      descEn: 'Calms sudden pitch instability.',
    },
    {
      key: 'autoTone',
      labelTr: 'Akıllı ton dengesi',
      labelEn: 'Smart tone balance',
      descTr: 'Büyük perde/formant kaymalarında gövde ve havayı dengeler.',
      descEn: 'Balances body and air tilt under large shifts.',
    },
  ];

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label={lang === 'tr' ? 'İnce Ayar ve Ön İşleme' : 'Fine Tune & Pre-Conditioning'}
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4 backdrop-blur-sm"
      onClick={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div className="w-full max-w-4xl max-h-[90vh] overflow-y-auto rounded-2xl border border-white/12 bg-[#10151F] p-5 sm:p-6 shadow-2xl">
        <div className="flex items-center justify-between border-b border-white/10 pb-3.5">
          <div>
            <h2 className="text-base font-bold text-slate-100">
              {lang === 'tr'
                ? 'İnce Ayar · Ön İşleme, Doku ve Auto-Tune Kalibrasyonu'
                : 'Fine Tune · Conditioning, Texture & Auto-Tune Calibration'}
            </h2>
            <p className="mt-0.5 text-xs text-slate-400">
              {lang === 'tr'
                ? 'Giriş filtresi, 4-bant vokal EQ, LF glottal doku ve stüdyo Auto-Tune parametreleri.'
                : 'Input conditioning, 4-band vocal EQ, LF glottal texture, and studio Auto-Tune controls.'}
            </p>
          </div>
          <button
            type="button"
            onClick={onClose}
            aria-label={lang === 'tr' ? 'İnce Ayar panelini kapat' : 'Close Fine Tune drawer'}
            className="inline-flex items-center gap-1.5 rounded-lg border border-white/10 bg-[#182030] px-3 py-1.5 text-xs font-medium text-slate-200 hover:bg-[#222D42] transition-colors whitespace-nowrap shrink-0"
          >
            <X className="h-3.5 w-3.5" />
            <span>{lang === 'tr' ? 'Kapat' : 'Close'}</span>
          </button>
        </div>

        <div className="mt-5 grid grid-cols-1 gap-6 md:grid-cols-2">
          {/* Group 1: Pre-Conditioning (New in v3.0) */}
          <div className="space-y-4">
            <h3 className="text-xs font-semibold tracking-wider text-amber-400">
              {lang === 'tr' ? 'Giriş Ön İşleme (DSP Öncesi)' : 'Input Pre-Conditioning (Pre-DSP)'}
            </h3>
            <div className="space-y-3.5 rounded-xl border border-white/8 bg-[#0B0F17] p-4">
              <SliderRow
                id="ft-hpf"
                label={lang === 'tr' ? 'HPF Alt Frekans Kesim' : 'HPF Rumble Cutoff'}
                value={params.hpfHz}
                min={40}
                max={160}
                step={5}
                display={`${Math.round(params.hpfHz)} Hz`}
                onChange={(v) => onChange({ hpfHz: v })}
              />
              <SliderRow
                id="ft-gate"
                label={lang === 'tr' ? 'Gürültü Kapısı Eşiği' : 'Noise Gate Floor'}
                value={params.gateDb}
                min={-70}
                max={-24}
                step={1}
                display={`${Math.round(params.gateDb)} dBFS`}
                onChange={(v) => onChange({ gateDb: v })}
              />
              <SliderRow
                id="ft-deesser"
                label={lang === 'tr' ? 'Dinamik De-Esser (5–10 kHz)' : 'Dynamic De-Esser (5–10 kHz)'}
                value={params.deEsser}
                min={0}
                max={1}
                step={0.01}
                display={formatPercent(params.deEsser)}
                onChange={(v) => onChange({ deEsser: v })}
              />
            </div>

            <h3 className="text-xs font-semibold tracking-wider text-cyan-400 pt-1">
              {lang === 'tr' ? 'Stüdyo Auto-Tune Kalibrasyonu' : 'Studio Auto-Tune Calibration'}
            </h3>
            <div className="space-y-3.5 rounded-xl border border-white/8 bg-[#0B0F17] p-4">
              <div className="grid grid-cols-2 gap-3">
                <div className="flex flex-col gap-1">
                  <label htmlFor="ft-scale" className="text-xs font-medium text-slate-300">
                    {lang === 'tr' ? 'Gam Modu' : 'Scale Mode'}
                  </label>
                  <select
                    id="ft-scale"
                    value={params.scaleMode}
                    onChange={(e) => onChange({ scaleMode: e.target.value as ScaleMode })}
                    className="rounded-lg border border-white/10 bg-[#141B27] px-2.5 py-1.5 text-xs text-slate-100 focus:border-amber-400 focus:outline-none"
                  >
                    {scaleOptions.map((opt) => (
                      <option key={opt.value} value={opt.value}>
                        {lang === 'tr' ? opt.labelTr : opt.labelEn}
                      </option>
                    ))}
                  </select>
                </div>
                <div className="flex flex-col gap-1">
                  <label htmlFor="ft-key" className="text-xs font-medium text-slate-300">
                    {lang === 'tr' ? 'Kök Nota (Manuel)' : 'Root Key (Manual)'}
                  </label>
                  <select
                    id="ft-key"
                    value={params.manualKey}
                    disabled={params.scaleMode === 'auto' || params.scaleMode === 'chromatic'}
                    onChange={(e) => onChange({ manualKey: parseInt(e.target.value, 10) })}
                    className="rounded-lg border border-white/10 bg-[#141B27] px-2.5 py-1.5 text-xs text-slate-100 disabled:opacity-45 focus:border-amber-400 focus:outline-none"
                  >
                    {NOTE_NAMES.map((n, idx) => (
                      <option key={n} value={idx}>
                        {n}
                      </option>
                    ))}
                  </select>
                </div>
              </div>
              <SliderRow
                id="ft-retune"
                label={lang === 'tr' ? 'Retune Hızı' : 'Retune Speed'}
                value={params.retuneSpeedMs}
                min={5}
                max={120}
                step={1}
                display={`${Math.round(params.retuneSpeedMs)} ms`}
                onChange={(v) => onChange({ retuneSpeedMs: v })}
              />
              <SliderRow
                id="ft-vibrato"
                label={lang === 'tr' ? 'Doğal Vibrato Koruma' : 'Natural Vibrato Preserve'}
                value={params.vibratoPreserve}
                min={0}
                max={1}
                step={0.01}
                display={formatPercent(params.vibratoPreserve)}
                onChange={(v) => onChange({ vibratoPreserve: v })}
              />
            </div>
          </div>

          {/* Group 2: Texture & 4-Band Studio Vocal EQ */}
          <div className="space-y-4">
            <h3 className="text-xs font-semibold tracking-wider text-amber-400">
              {lang === 'tr' ? 'Ses Dokusu ve 4-Bant EQ' : 'Vocal Texture & 4-Band EQ'}
            </h3>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-x-5 gap-y-3.5 rounded-xl border border-white/8 bg-[#0B0F17] p-4">
              <SliderRow
                id="ft-growl"
                label={lang === 'tr' ? 'Hırıltı (F0/2)' : 'Growl (F0/2)'}
                value={params.growl}
                min={0}
                max={1}
                step={0.01}
                display={formatPercent(params.growl)}
                onChange={(v) => onChange({ growl: v })}
              />
              <SliderRow
                id="ft-breath"
                label={lang === 'tr' ? 'Nefes Dokusu' : 'Breath Aspiration'}
                value={params.breath}
                min={0}
                max={1}
                step={0.01}
                display={formatPercent(params.breath)}
                onChange={(v) => onChange({ breath: v })}
              />
              <SliderRow
                id="ft-age"
                label={lang === 'tr' ? 'Yaş' : 'Age'}
                value={params.age}
                min={-50}
                max={50}
                step={1}
                display={`${params.age > 0 ? '+' : ''}${Math.round(params.age)}`}
                onChange={(v) => onChange({ age: v })}
              />
              <SliderRow
                id="ft-nasal"
                label={lang === 'tr' ? 'Geniz (1.15 kHz)' : 'Nasal (1.15 kHz)'}
                value={params.nasal}
                min={0}
                max={1}
                step={0.01}
                display={formatPercent(params.nasal)}
                onChange={(v) => onChange({ nasal: v })}
              />
              <SliderRow
                id="ft-body"
                label={lang === 'tr' ? 'Gövde (170 Hz)' : 'Body (170 Hz)'}
                value={params.body}
                min={-6}
                max={6}
                step={0.1}
                display={formatDb(params.body)}
                onChange={(v) => onChange({ body: v })}
              />
              <SliderRow
                id="ft-presence"
                label={lang === 'tr' ? 'Netlik (3.2 kHz)' : 'Presence (3.2 kHz)'}
                value={params.presence}
                min={-6}
                max={6}
                step={0.1}
                display={formatDb(params.presence)}
                onChange={(v) => onChange({ presence: v })}
              />
              <SliderRow
                id="ft-air"
                label={lang === 'tr' ? 'Hava (8.8 kHz)' : 'Air (8.8 kHz)'}
                value={params.air}
                min={-6}
                max={6}
                step={0.1}
                display={formatDb(params.air)}
                onChange={(v) => onChange({ air: v })}
              />
              <SliderRow
                id="ft-output"
                label={lang === 'tr' ? 'Çıkış Kazancı' : 'Output Trim'}
                value={params.output}
                min={-12}
                max={12}
                step={0.1}
                display={formatDb(params.output)}
                onChange={(v) => onChange({ output: v })}
              />
            </div>

            <h3 className="text-xs font-semibold tracking-wider text-slate-300 pt-1">
              {lang === 'tr' ? 'Motor Dinamik Anahtarları' : 'Engine Dynamic Toggles'}
            </h3>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-2.5">
              {toggles.map((item) => {
                const active = params[item.key];
                return (
                  <button
                    key={item.key}
                    type="button"
                    aria-pressed={active}
                    onClick={() => onChange({ [item.key]: !active })}
                    className={`rounded-xl border p-3 text-left transition-colors ${
                      active
                        ? 'border-amber-400/50 bg-amber-500/10 text-slate-100'
                        : 'border-white/8 bg-[#0B0F17] text-slate-400 hover:border-white/15'
                    }`}
                  >
                    <div className="flex items-center justify-between gap-2">
                      <span className="text-xs font-semibold">
                        {lang === 'tr' ? item.labelTr : item.labelEn}
                      </span>
                      <span
                        className={`font-mono text-[10px] ${
                          active ? 'text-amber-300' : 'text-slate-500'
                        }`}
                      >
                        {active ? (lang === 'tr' ? 'AÇIK' : 'ON') : lang === 'tr' ? 'KAPALI' : 'OFF'}
                      </span>
                    </div>
                    <p className="mt-1 text-[11px] leading-snug text-slate-400">
                      {lang === 'tr' ? item.descTr : item.descEn}
                    </p>
                  </button>
                );
              })}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
