import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  Download,
  Info,
  Link2,
  Mic,
  Play,
  RotateCcw,
  SlidersHorizontal,
  Square,
  Upload,
} from 'lucide-react';
import { type InputSourceKind, StudioAudioEngine } from './audio/studioAudioEngine';
import { BUILTIN_STEMS } from './audio/vocalTestSynth';
import { AboutModal } from './components/AboutModal';
import { EngineeringAuditView } from './components/EngineeringAuditView';
import { FineTuneDrawer } from './components/FineTuneDrawer';
import { OscilloscopeLens } from './components/OscilloscopeLens';
import { RotaryKnob } from './components/RotaryKnob';
import { SpectrumAnalyzerView } from './components/SpectrumAnalyzerView';
import {
  applyCharacterToParams,
  CHARACTER_PRESETS,
  clamp,
  formatKeyScale,
  formatPercent,
  formatSemitones,
  formatSignedPercent,
  LINK_RATIO,
  NOTE_NAMES,
  voiceTypeLabel,
} from './dsp/characters';
import {
  type AudioTelemetry,
  DEFAULT_STUDIO_PARAMS,
  type ScaleMode,
  type StudioParams,
} from './dsp/types';

type ActiveTab = 'console' | 'spectrum' | 'audit';

const INITIAL_TELEMETRY: AudioTelemetry = {
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
  waveHistory: new Array<number>(64).fill(0),
  drySpectrum: new Array<number>(64).fill(-72),
  wetSpectrum: new Array<number>(64).fill(-72),
  pitchTrace: [],
  summary: {
    medianF0: 0,
    lowF0: 0,
    highF0: 0,
    formants: [0, 0, 0],
    brightness: 0,
    breathiness: 0,
    seconds: 0,
    type: 'Unknown',
    ready: false,
  },
};

export function App() {
  const [lang, setLang] = useState<'tr' | 'en'>('tr');
  const [activeTab, setActiveTab] = useState<ActiveTab>('console');
  const [params, setParams] = useState<StudioParams>(DEFAULT_STUDIO_PARAMS);
  const [abSlot, setAbSlot] = useState<'A' | 'B'>('A');
  const slotBRef = useRef<StudioParams>({
    ...DEFAULT_STUDIO_PARAMS,
    character: 0,
    pitch: 0,
    formant: 0,
    regen: 0,
    glottal: 0,
    growl: 0,
    breath: 0,
    body: 0,
    air: 0,
  });

  const [source, setSource] = useState<InputSourceKind>('baritone');
  const [isPlaying, setIsPlaying] = useState(false);
  const [uploadedName, setUploadedName] = useState('');
  const [audioError, setAudioError] = useState<string | null>(null);
  const [telemetry, setTelemetry] = useState<AudioTelemetry>(INITIAL_TELEMETRY);
  const [fineTuneOpen, setFineTuneOpen] = useState(false);
  const [aboutOpen, setAboutOpen] = useState(false);
  const [exportMessage, setExportMessage] = useState<string | null>(null);

  const engineRef = useRef<StudioAudioEngine | null>(null);
  const fileInputRef = useRef<HTMLInputElement | null>(null);

  if (!engineRef.current) {
    engineRef.current = new StudioAudioEngine(DEFAULT_STUDIO_PARAMS);
  }

  useEffect(() => {
    engineRef.current?.updateParams(params);
  }, [params]);

  useEffect(() => {
    const id = window.setInterval(() => {
      if (!engineRef.current) return;
      const next = engineRef.current.pollTelemetry(0.033);
      setTelemetry(next);
    }, 33);
    return () => {
      window.clearInterval(id);
    };
  }, []);

  useEffect(() => {
    return () => {
      engineRef.current?.dispose();
    };
  }, []);

  const updateParams = useCallback((patch: Partial<StudioParams>) => {
    setParams((prev) => ({ ...prev, ...patch }));
  }, []);

  // Synchronized Pitch/Formant Link (fixes v2.0 double-offset bug!)
  const handlePitchChange = useCallback((nextPitch: number) => {
    setParams((prev) => {
      const dp = nextPitch - prev.pitch;
      const nextFormant = prev.follow
        ? clamp(Number((prev.formant + dp * LINK_RATIO).toFixed(2)), -8, 8)
        : prev.formant;
      return { ...prev, pitch: nextPitch, formant: nextFormant };
    });
  }, []);

  const handleFormantChange = useCallback((nextFormant: number) => {
    setParams((prev) => {
      const df = nextFormant - prev.formant;
      const nextPitch = prev.follow
        ? clamp(Number((prev.pitch + df / LINK_RATIO).toFixed(2)), -18, 18)
        : prev.pitch;
      return { ...prev, formant: nextFormant, pitch: nextPitch };
    });
  }, []);

  const handleSelectCharacter = useCallback(
    (index: number) => {
      setParams((prev) => applyCharacterToParams(prev, index, telemetry.summary));
    },
    [telemetry.summary]
  );

  const handleToggleAB = () => {
    const other = slotBRef.current;
    slotBRef.current = params;
    setParams(other);
    setAbSlot((s) => (s === 'A' ? 'B' : 'A'));
  };

  const handleTogglePlay = async () => {
    setAudioError(null);
    const eng = engineRef.current;
    if (!eng) return;
    if (isPlaying) {
      eng.stop();
      setIsPlaying(false);
    } else {
      try {
        await eng.start();
        setIsPlaying(true);
      } catch (err) {
        setAudioError(
          err instanceof Error
            ? err.message
            : lang === 'tr'
              ? 'Ses aygıtı başlatılamadı.'
              : 'Could not start audio device.'
        );
        setIsPlaying(false);
      }
    }
  };

  const handleSelectSource = async (next: InputSourceKind) => {
    setAudioError(null);
    setSource(next);
    const eng = engineRef.current;
    if (!eng) return;
    try {
      await eng.selectSource(next, isPlaying || next === 'mic');
      setIsPlaying(eng.getIsPlaying());
    } catch (err) {
      setAudioError(
        err instanceof Error
          ? err.message
          : lang === 'tr'
            ? 'Mikrofon izni reddedildi veya aygıt bulunamadı.'
            : 'Microphone access denied or unavailable.'
      );
      setIsPlaying(false);
    }
  };

  const handleFileChange = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file || !engineRef.current) return;
    setAudioError(null);
    try {
      await engineRef.current.loadAudioFile(file);
      setSource('file');
      setUploadedName(file.name);
      setIsPlaying(true);
    } catch {
      setAudioError(
        lang === 'tr'
          ? 'Ses dosyası çözümlenemedi. WAV, MP3 veya OGG deneyin.'
          : 'Could not decode audio file. Try WAV, MP3, or OGG.'
      );
    } finally {
      e.target.value = '';
    }
  };

  const handleExportWav = () => {
    if (!engineRef.current) return;
    const { blob, filename } = engineRef.current.exportProcessedWav();
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
    setExportMessage(
      lang === 'tr' ? `${filename} dışa aktarıldı` : `Exported ${filename}`
    );
    window.setTimeout(() => setExportMessage(null), 3200);
  };

  const handleRelearn = () => {
    engineRef.current?.resetVoiceProfile();
  };

  const profileText = telemetry.summary.ready
    ? `${voiceTypeLabel(telemetry.summary.type, lang)} · ${Math.round(telemetry.summary.medianF0)} Hz · ${formatKeyScale(
        params.scaleMode === 'auto' ? telemetry.detectedKey : params.manualKey,
        params.scaleMode === 'auto' ? telemetry.detectedScale : params.scaleMode,
        lang
      )}`
    : isPlaying
      ? `${lang === 'tr' ? 'Ses analiz ediliyor' : 'Learning voice'} · ${telemetry.summary.seconds.toFixed(1)}s`
      : lang === 'tr'
        ? 'Profil hazır · Oynatın'
        : 'Profile ready · Play';

  const renderToggleBtn = (
    label: string,
    active: boolean,
    onClick: () => void,
    tone: 'amber' | 'cyan' = 'amber'
  ) => (
    <button
      type="button"
      aria-pressed={active}
      onClick={onClick}
      className={`flex items-center justify-center gap-2 rounded-lg border px-3 py-2 text-xs font-semibold transition-colors whitespace-nowrap shrink-0 ${
        active
          ? tone === 'cyan'
            ? 'border-cyan-400/50 bg-cyan-500/12 text-cyan-200'
            : 'border-amber-400/50 bg-amber-500/12 text-amber-200'
          : 'border-white/10 bg-[#0D1119] text-slate-400 hover:border-white/20 hover:text-slate-200'
      }`}
    >
      <span
        aria-hidden="true"
        className={`h-2 w-2 rounded-full ${
          active
            ? tone === 'cyan'
              ? 'bg-cyan-400'
              : 'bg-amber-400'
            : 'bg-slate-600'
        }`}
      />
      <span>{label}</span>
    </button>
  );

  return (
    <div className="min-h-screen bg-[#090C11] text-slate-100 flex flex-col">
      {/* Strict 3-Zone Top Bar Contract */}
      <header className="border-b border-white/8 bg-[#0D1118]/95">
        <div className="mx-auto flex max-w-[1360px] items-center justify-between gap-2.5 sm:gap-8 px-3 sm:px-6 py-3">
          {/* Zone 1: Single-line brand wordmark */}
          <button
            type="button"
            onClick={() => setActiveTab('console')}
            className="text-lg sm:text-xl font-extrabold tracking-tight text-slate-100 whitespace-nowrap shrink-0 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-amber-400 rounded"
          >
            Alptug<span className="text-[#E5484D]">DUB</span>
          </button>

          {/* Zone 2: Concise single-line navigation links */}
          <nav
            aria-label={lang === 'tr' ? 'Ana Görünümler' : 'Primary Views'}
            className="flex items-center gap-3 sm:gap-7"
          >
            {(
              [
                { id: 'console', tr: 'Konsol', trShort: 'Konsol', en: 'Console', enShort: 'Console' },
                { id: 'spectrum', tr: 'Spektrum', trShort: 'Spektrum', en: 'Spectrum', enShort: 'Spectrum' },
                { id: 'audit', tr: 'Mühendislik Raporu', trShort: 'Rapor', en: 'DSP Audit', enShort: 'Audit' },
              ] as const
            ).map((tab) => {
              const isCurrent = activeTab === tab.id;
              return (
                <button
                  key={tab.id}
                  type="button"
                  onClick={() => setActiveTab(tab.id)}
                  aria-current={isCurrent ? 'page' : undefined}
                  className={`relative py-1 text-xs sm:text-sm font-medium transition-colors whitespace-nowrap shrink-0 ${
                    isCurrent
                      ? 'text-amber-300'
                      : 'text-slate-400 hover:text-slate-100'
                  }`}
                >
                  <span className="sm:hidden">{lang === 'tr' ? tab.trShort : tab.enShort}</span>
                  <span className="hidden sm:inline">{lang === 'tr' ? tab.tr : tab.en}</span>
                  {isCurrent && (
                    <span className="absolute inset-x-0 -bottom-[13px] h-0.5 bg-amber-400" />
                  )}
                </button>
              );
            })}
            <button
              type="button"
              onClick={() => setAboutOpen(true)}
              className="hidden sm:inline-flex items-center gap-1 py-1 text-sm font-medium text-slate-400 hover:text-slate-100 transition-colors whitespace-nowrap shrink-0"
            >
              {lang === 'tr' ? 'Hakkında' : 'About'}
            </button>
          </nav>

          {/* Zone 3: Single Primary Action */}
          <div className="shrink-0">
            <button
              type="button"
              onClick={handleExportWav}
              className="inline-flex items-center gap-1.5 sm:gap-2 rounded-lg bg-amber-500 px-2.5 sm:px-3.5 py-2 text-xs font-bold text-slate-950 hover:bg-amber-400 transition-colors whitespace-nowrap shrink-0"
            >
              <Download className="h-3.5 w-3.5" />
              <span className="sm:hidden">WAV</span>
              <span className="hidden sm:inline">{lang === 'tr' ? 'WAV Dışa Aktar' : 'Export WAV'}</span>
            </button>
          </div>
        </div>
      </header>

      {/* Main Walnut-Cheeked Hardware Console Frame */}
      <main className="mx-auto w-full max-w-[1360px] flex-1 px-2 sm:px-6 py-4 sm:py-6">
        <div className="relative flex overflow-hidden rounded-2xl border border-white/10 bg-[#111620] shadow-2xl">
          {/* Left Oiled Walnut Wood Cheek */}
          <div
            aria-hidden="true"
            className="walnut-cheek hidden lg:block w-7 shrink-0 border-r border-black/60"
          />

          {/* Inner Anodized Studio Chassis */}
          <div className="flex-1 min-w-0 p-3.5 sm:p-5 space-y-4">
            {/* Studio Source, Transport & Profile Toolbar */}
            <section
              aria-label={lang === 'tr' ? 'Ses Kaynağı ve Oynatma Kontrolleri' : 'Audio Source & Transport'}
              className="flex flex-wrap items-center justify-between gap-3 rounded-xl border border-white/8 bg-[#0B0F17] px-3.5 py-2.5"
            >
              {/* Left: Play/Stop + Stem Selector + File Upload + Live Mic */}
              <div className="flex flex-wrap items-center gap-2">
                <button
                  type="button"
                  onClick={handleTogglePlay}
                  aria-label={isPlaying ? (lang === 'tr' ? 'Durdur' : 'Stop') : lang === 'tr' ? 'Oynat' : 'Play'}
                  className={`inline-flex items-center gap-1.5 rounded-lg px-3.5 py-2 text-xs font-bold transition-colors whitespace-nowrap shrink-0 ${
                    isPlaying
                      ? 'bg-rose-500 text-white hover:bg-rose-400'
                      : 'bg-cyan-400 text-slate-950 hover:bg-cyan-300'
                  }`}
                >
                  {isPlaying ? <Square className="h-3.5 w-3.5 fill-current" /> : <Play className="h-3.5 w-3.5 fill-current" />}
                  <span>{isPlaying ? (lang === 'tr' ? 'Durdur' : 'Stop') : lang === 'tr' ? 'Oynat' : 'Play'}</span>
                </button>

                <div className="flex flex-wrap items-center gap-1 rounded-lg bg-[#141A26] p-1">
                  {BUILTIN_STEMS.map((stem) => {
                    const active = source === stem.id;
                    return (
                      <button
                        key={stem.id}
                        type="button"
                        onClick={() => handleSelectSource(stem.id)}
                        className={`rounded-md px-2.5 py-1 text-xs font-medium transition-colors whitespace-nowrap shrink-0 ${
                          active
                            ? 'bg-[#212B3D] text-amber-300'
                            : 'text-slate-400 hover:text-slate-200'
                        }`}
                      >
                        {lang === 'tr' ? stem.nameTr : stem.nameEn}
                      </button>
                    );
                  })}

                  <button
                    type="button"
                    onClick={() => fileInputRef.current?.click()}
                    className={`inline-flex items-center gap-1 rounded-md px-2.5 py-1 text-xs font-medium transition-colors whitespace-nowrap shrink-0 ${
                      source === 'file'
                        ? 'bg-[#212B3D] text-amber-300'
                        : 'text-slate-400 hover:text-slate-200'
                    }`}
                  >
                    <Upload className="h-3 w-3" />
                    <span className="max-w-[110px] truncate">
                      {source === 'file' && uploadedName
                        ? uploadedName
                        : lang === 'tr'
                          ? 'Dosya Yükle'
                          : 'Load Audio'}
                    </span>
                  </button>

                  <button
                    type="button"
                    onClick={() => handleSelectSource('mic')}
                    className={`inline-flex items-center gap-1 rounded-md px-2.5 py-1 text-xs font-medium transition-colors whitespace-nowrap shrink-0 ${
                      source === 'mic'
                        ? 'bg-rose-500/20 text-rose-300'
                        : 'text-slate-400 hover:text-slate-200'
                    }`}
                  >
                    <Mic className="h-3 w-3" />
                    <span>{lang === 'tr' ? 'Mikrofon' : 'Mic'}</span>
                  </button>
                </div>

                <input
                  ref={fileInputRef}
                  type="file"
                  accept="audio/*"
                  onChange={handleFileChange}
                  className="hidden"
                />
              </div>

              {/* Right: Voice Profile Readout + A/B + Bypass + Fine Tune + TR/EN */}
              <div className="flex flex-wrap items-center gap-2">
                <div className="flex items-center gap-1.5 rounded-lg border border-white/8 bg-[#080B10] px-3 py-1.5 font-mono text-xs tabular-nums text-amber-300">
                  <span className="max-w-[230px] truncate">{profileText}</span>
                  <button
                    type="button"
                    onClick={handleRelearn}
                    aria-label={lang === 'tr' ? 'Ses profilini sıfırla ve yeniden öğren' : 'Relearn voice profile'}
                    title={lang === 'tr' ? 'Sesimi ve gamı yeniden öğren' : 'Relearn voice & key'}
                    className="ml-1 rounded p-0.5 text-slate-400 hover:text-slate-100 transition-colors"
                  >
                    <RotateCcw className="h-3.5 w-3.5" />
                  </button>
                </div>

                <button
                  type="button"
                  onClick={handleToggleAB}
                  className="rounded-lg border border-white/10 bg-[#141A26] px-2.5 py-1.5 font-mono text-xs font-semibold text-slate-200 hover:border-amber-400/50 transition-colors whitespace-nowrap shrink-0"
                >
                  A/B: {abSlot}
                </button>

                <button
                  type="button"
                  aria-pressed={params.bypass}
                  onClick={() => updateParams({ bypass: !params.bypass })}
                  className={`rounded-lg border px-2.5 py-1.5 text-xs font-semibold transition-colors whitespace-nowrap shrink-0 ${
                    params.bypass
                      ? 'border-rose-400/60 bg-rose-500/20 text-rose-200'
                      : 'border-white/10 bg-[#141A26] text-slate-300 hover:border-white/25'
                  }`}
                >
                  Bypass
                </button>

                <button
                  type="button"
                  onClick={() => setFineTuneOpen(true)}
                  className="inline-flex items-center gap-1.5 rounded-lg border border-white/10 bg-[#161E2D] px-3 py-1.5 text-xs font-semibold text-slate-200 hover:border-amber-400/50 hover:text-amber-300 transition-colors whitespace-nowrap shrink-0"
                >
                  <SlidersHorizontal className="h-3.5 w-3.5" />
                  <span>{lang === 'tr' ? 'İnce Ayar' : 'Fine Tune'}</span>
                </button>

                <button
                  type="button"
                  onClick={() => setLang((l) => (l === 'tr' ? 'en' : 'tr'))}
                  aria-label="Dil / Language"
                  className="rounded-lg border border-white/10 bg-[#141A26] px-2 py-1.5 font-mono text-[11px] font-semibold text-slate-400 hover:text-slate-100 transition-colors whitespace-nowrap shrink-0"
                >
                  {lang.toUpperCase()}
                </button>

                <button
                  type="button"
                  onClick={() => setAboutOpen(true)}
                  aria-label={lang === 'tr' ? 'Hakkında' : 'About'}
                  className="sm:hidden rounded-lg border border-white/10 bg-[#141A26] p-1.5 text-slate-400 hover:text-slate-100"
                >
                  <Info className="h-4 w-4" />
                </button>
              </div>
            </section>

            {/* Error or Export confirmation banner */}
            {audioError && (
              <div
                role="alert"
                className="rounded-lg border border-rose-500/40 bg-rose-950/40 px-4 py-2 text-xs text-rose-200"
              >
                {audioError}
              </div>
            )}
            {exportMessage && (
              <div
                role="status"
                className="rounded-lg border border-emerald-500/40 bg-emerald-950/40 px-4 py-2 text-xs text-emerald-200"
              >
                {exportMessage}
              </div>
            )}

            {/* Primary View Switcher */}
            {activeTab === 'console' && (
              <>
                {/* Oscilloscope Lens */}
                <OscilloscopeLens
                  telemetry={telemetry}
                  params={params}
                  isPlaying={isPlaying}
                  lang={lang}
                />

                {/* 3-Module Symmetrical Analog Studio Deck */}
                <section
                  aria-label={lang === 'tr' ? 'Ana Stüdyo Kontrol Modülleri' : 'Studio Control Modules'}
                  className="grid grid-cols-1 gap-4 lg:grid-cols-12 items-stretch"
                >
                  {/* Left Module: Hybrid & Auto-Tune */}
                  <div className="lg:col-span-3 rounded-xl border border-white/10 bg-[#0D121B] p-4 flex flex-col justify-between gap-4">
                    <h2 className="text-center text-xs font-semibold tracking-wider text-slate-400">
                      {lang === 'tr' ? 'Hibrit & Auto-Tune' : 'Hybrid & Auto-Tune'}
                    </h2>

                    <div className="flex items-center justify-around gap-2">
                      <RotaryKnob
                        id="regen"
                        label={lang === 'tr' ? 'Üretim' : 'Regen'}
                        value={params.regen}
                        min={0}
                        max={1}
                        step={0.01}
                        defaultValue={0.92}
                        formatValue={formatPercent}
                        onChange={(v) => updateParams({ regen: v })}
                        tooltip={
                          lang === 'tr'
                            ? 'LF glottal yeniden üretim oranı (0% spektral, 100% tam hibrit)'
                            : 'Hybrid LF glottal resynthesis amount'
                        }
                      />
                      <RotaryKnob
                        id="autotune"
                        label="Auto-Tune"
                        value={params.autotune}
                        min={0}
                        max={1}
                        step={0.01}
                        defaultValue={0}
                        formatValue={formatPercent}
                        accent="cyan"
                        onChange={(v) => updateParams({ autotune: v })}
                        tooltip={
                          lang === 'tr'
                            ? 'Uyarlanabilir insan sesi perde düzeltici'
                            : 'Adaptive human-voice pitch corrector'
                        }
                      />
                    </div>

                    {/* Quick Scale/Key bar when Auto-Tune > 0 */}
                    {params.autotune > 0.01 && (
                      <div className="grid grid-cols-2 gap-1.5 border-t border-white/8 pt-2.5">
                        <select
                          aria-label={lang === 'tr' ? 'Auto-Tune Gamı' : 'Auto-Tune Scale'}
                          value={params.scaleMode}
                          onChange={(e) => updateParams({ scaleMode: e.target.value as ScaleMode })}
                          className="rounded-md border border-white/10 bg-[#090C12] px-2 py-1 text-[11px] text-slate-200 focus:border-cyan-400 focus:outline-none"
                        >
                          <option value="auto">{lang === 'tr' ? 'Oto Gam' : 'Auto'}</option>
                          <option value="major">{lang === 'tr' ? 'Majör' : 'Major'}</option>
                          <option value="minor">{lang === 'tr' ? 'Minör' : 'Minor'}</option>
                          <option value="chromatic">{lang === 'tr' ? 'Kromatik' : 'Chromatic'}</option>
                          <option value="hijaz">{lang === 'tr' ? 'Hicaz' : 'Hijaz'}</option>
                          <option value="pentatonic">{lang === 'tr' ? 'Pentatonik' : 'Pentatonic'}</option>
                        </select>
                        <select
                          aria-label={lang === 'tr' ? 'Auto-Tune Kök Nota' : 'Auto-Tune Key'}
                          value={params.manualKey}
                          disabled={params.scaleMode === 'auto' || params.scaleMode === 'chromatic'}
                          onChange={(e) => updateParams({ manualKey: parseInt(e.target.value, 10) })}
                          className="rounded-md border border-white/10 bg-[#090C12] px-2 py-1 text-[11px] text-slate-200 disabled:opacity-40 focus:border-cyan-400 focus:outline-none"
                        >
                          {NOTE_NAMES.map((n, i) => (
                            <option key={n} value={i}>
                              {n}
                            </option>
                          ))}
                        </select>
                      </div>
                    )}

                    <div className="grid grid-cols-2 gap-2">
                      {renderToggleBtn(
                        lang === 'tr' ? 'Yapay Zekâ' : 'AI Pitch',
                        params.aiPitch,
                        () => updateParams({ aiPitch: !params.aiPitch }),
                        'cyan'
                      )}
                      {renderToggleBtn(
                        lang === 'tr' ? 'Ünsüz Koruma' : 'Consonant',
                        params.consonantGuard,
                        () => updateParams({ consonantGuard: !params.consonantGuard }),
                        'amber'
                      )}
                    </div>
                  </div>

                  {/* Center Module: Voice Core (Focal Anchor) */}
                  <div className="lg:col-span-6 rounded-xl border border-amber-500/20 bg-[#0F1520] p-4 flex flex-col justify-between gap-3">
                    <div className="flex items-center justify-between px-1">
                      <span className="text-[11px] font-mono text-slate-500">
                        HPF {Math.round(params.hpfHz)} Hz
                      </span>
                      <h2 className="text-xs font-bold tracking-wider text-amber-400">
                        {lang === 'tr' ? 'Ana Dönüşüm' : 'Voice Core'}
                      </h2>
                      <span className="text-[11px] font-mono text-slate-500">
                        -0.3 dBFS Limiter
                      </span>
                    </div>

                    <div className="flex flex-wrap sm:flex-nowrap items-center justify-around gap-4 py-1">
                      <RotaryKnob
                        id="pitch"
                        label={lang === 'tr' ? 'Perde' : 'Pitch'}
                        value={params.pitch}
                        min={-18}
                        max={18}
                        step={0.1}
                        defaultValue={0}
                        bipolar
                        size="lg"
                        formatValue={formatSemitones}
                        onChange={handlePitchChange}
                        tooltip={
                          lang === 'tr'
                            ? 'Sesin perdesi (±18 yarım ton). Çift tık: sıfırla'
                            : 'Voice pitch (±18 semitones). Double-click to reset'
                        }
                      />

                      <button
                        type="button"
                        aria-pressed={params.follow}
                        onClick={() => updateParams({ follow: !params.follow })}
                        className={`inline-flex items-center gap-1.5 rounded-lg border px-3 py-2 text-xs font-semibold transition-colors whitespace-nowrap shrink-0 ${
                          params.follow
                            ? 'border-cyan-400/60 bg-cyan-500/15 text-cyan-200'
                            : 'border-white/10 bg-[#0B0F17] text-slate-400 hover:border-white/20 hover:text-slate-200'
                        }`}
                      >
                        <Link2 className="h-3.5 w-3.5" />
                        <span>{lang === 'tr' ? 'Bağla' : 'Link'}</span>
                      </button>

                      <RotaryKnob
                        id="formant"
                        label={lang === 'tr' ? 'Gırtlak' : 'Formant'}
                        value={params.formant}
                        min={-8}
                        max={8}
                        step={0.1}
                        defaultValue={0}
                        bipolar
                        size="lg"
                        formatValue={formatSemitones}
                        onChange={handleFormantChange}
                        tooltip={
                          lang === 'tr'
                            ? 'Doğrusal olmayan F1/F2/F3 ses yolu bükme (±8 st)'
                            : 'Non-linear F1/F2/F3 vocal tract warp (±8 st)'
                        }
                      />
                    </div>

                    <div className="flex items-center justify-center gap-4 border-t border-white/6 pt-2 text-[11px] text-slate-400 font-mono tabular-nums">
                      <span>
                        {lang === 'tr' ? 'Hırıltı' : 'Growl'}: {formatPercent(params.growl)}
                      </span>
                      <span aria-hidden="true">·</span>
                      <span>
                        {lang === 'tr' ? 'Nefes' : 'Breath'}: {formatPercent(params.breath)}
                      </span>
                      <span aria-hidden="true">·</span>
                      <span>
                        De-Ess: {formatPercent(params.deEsser)}
                      </span>
                    </div>
                  </div>

                  {/* Right Module: Texture & Mix */}
                  <div className="lg:col-span-3 rounded-xl border border-white/10 bg-[#0D121B] p-4 flex flex-col justify-between gap-4">
                    <h2 className="text-center text-xs font-semibold tracking-wider text-slate-400">
                      {lang === 'tr' ? 'Doku & Miks' : 'Tone & Mix'}
                    </h2>

                    <div className="flex items-center justify-around gap-2">
                      <RotaryKnob
                        id="glottal"
                        label={lang === 'tr' ? 'Doku' : 'Glottal'}
                        value={params.glottal}
                        min={-1}
                        max={1}
                        step={0.01}
                        defaultValue={0}
                        bipolar
                        formatValue={formatSignedPercent}
                        onChange={(v) => updateParams({ glottal: v })}
                        tooltip={
                          lang === 'tr'
                            ? 'Eksi: güçlü göğüs sesi. Artı: yumuşak kafa sesi (LF Oq)'
                            : 'Minus: pressed chest voice. Plus: airy head voice (LF Oq)'
                        }
                      />
                      <RotaryKnob
                        id="mix"
                        label={lang === 'tr' ? 'Karışım' : 'Mix'}
                        value={params.mix}
                        min={0}
                        max={1}
                        step={0.01}
                        defaultValue={1}
                        formatValue={formatPercent}
                        onChange={(v) => updateParams({ mix: v })}
                        tooltip={
                          lang === 'tr'
                            ? 'İşlenmiş ses ile orijinal ses karışımı'
                            : 'Wet / dry signal mix'
                        }
                      />
                    </div>

                    <div className="grid grid-cols-2 gap-2">
                      {renderToggleBtn(
                        lang === 'tr' ? 'Sesime Uyarla' : 'Auto-Match',
                        params.autoMatch,
                        () => updateParams({ autoMatch: !params.autoMatch }),
                        'amber'
                      )}
                      {renderToggleBtn(
                        lang === 'tr' ? 'Seviye Koru' : 'Keep Level',
                        params.loudness,
                        () => updateParams({ loudness: !params.loudness }),
                        'amber'
                      )}
                    </div>
                  </div>
                </section>

                {/* Bottom 13-Character Analog Console Key Bank (7 + 6 full-width rows on desktop) */}
                <section
                  aria-label={lang === 'tr' ? 'Karakter Ses Bankası' : 'Character Voice Bank'}
                  className="rounded-xl border border-white/10 bg-[#0B0F17] p-3"
                >
                  <div className="flex flex-wrap gap-2">
                    {CHARACTER_PRESETS.map((char) => {
                      const active = params.character === char.index;
                      const rowWidthClass =
                        char.index < 7
                          ? 'basis-[calc((100%-0.55rem)/2)] sm:basis-[calc((100%-1.55rem)/4)] md:basis-[calc((100%-3.1rem)/7)]'
                          : 'basis-[calc((100%-0.55rem)/2)] sm:basis-[calc((100%-1.55rem)/4)] md:basis-[calc((100%-2.6rem)/6)]';
                      return (
                        <button
                          key={char.id}
                          type="button"
                          aria-pressed={active}
                          onClick={() => handleSelectCharacter(char.index)}
                          className={`group relative grow flex flex-col items-center justify-center rounded-lg border px-2.5 py-2.5 text-xs font-semibold transition-colors whitespace-nowrap ${rowWidthClass} ${
                            active
                              ? 'border-amber-400/70 bg-amber-500/14 text-amber-200'
                              : 'border-white/8 bg-[#121722] text-slate-300 hover:border-white/20 hover:bg-[#171E2C] hover:text-white'
                          }`}
                        >
                          <span
                            aria-hidden="true"
                            className={`mb-1.5 h-1 w-6 rounded-full transition-colors ${
                              active ? 'bg-amber-400' : 'bg-white/10 group-hover:bg-white/25'
                            }`}
                          />
                          <span className="truncate max-w-full">
                            {lang === 'tr' ? char.nameTr : char.nameEn}
                          </span>
                        </button>
                      );
                    })}
                  </div>
                </section>
              </>
            )}

            {activeTab === 'spectrum' && (
              <SpectrumAnalyzerView
                telemetry={telemetry}
                params={params}
                isPlaying={isPlaying}
                lang={lang}
              />
            )}

            {activeTab === 'audit' && <EngineeringAuditView lang={lang} />}
          </div>

          {/* Right Oiled Walnut Wood Cheek */}
          <div
            aria-hidden="true"
            className="walnut-cheek hidden lg:block w-7 shrink-0 border-l border-black/60"
          />
        </div>
      </main>

      {/* Quiet Studio Footer */}
      <footer className="border-t border-white/6 py-3 px-6 text-center text-xs text-slate-500">
        AlptugDUB Studio 3.0 · Alptuğ Dağ ·{' '}
        {lang === 'tr'
          ? '48 kHz Hibrit LF Glottal & Doğrusal Olmayan Formant Motoru'
          : '48 kHz Hybrid LF Glottal & Non-Linear Formant Engine'}
      </footer>

      {/* Modals */}
      <FineTuneDrawer
        open={fineTuneOpen}
        params={params}
        onChange={updateParams}
        onClose={() => setFineTuneOpen(false)}
        lang={lang}
      />
      <AboutModal open={aboutOpen} onClose={() => setAboutOpen(false)} lang={lang} />
    </div>
  );
}

export default App;
