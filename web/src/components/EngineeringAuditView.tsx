import React, { useState } from 'react';
import { CheckCircle2, Play, ShieldAlert } from 'lucide-react';
import { runEngineeringDiagnostics } from '../dsp/signalChain';
import type { DiagnosticCheckResult } from '../dsp/types';

interface EngineeringAuditViewProps {
  lang: 'tr' | 'en';
}

const AUDIT_DEFECTS = [
  {
    num: '01',
    module: 'HybridRegenerator.h',
    titleTr: 'Dairesel Konvolüsyon Örtüşmesi (Circular Aliasing)',
    titleEn: 'Circular Convolution Wrap-Around Aliasing',
    issueTr:
      'v2.0, 1024-noktalı STFT penceresini sıfır dolgulamadan (zero-padding) minimum-fazlı 1/H(z) ters filtresiyle çarpıp IFFT alıyordu; dürtü yanıtı pencere sonundan başa sarılarak metalik ön-yankı oluşturuyordu.',
    issueEn:
      'v2.0 multiplied unpadded 1024-pt STFT frames by minimum-phase inverse filters, causing impulse wrap-around aliasing and metallic pre-ringing.',
    fixTr:
      'v3.0, 2× sıfır dolgulu (2048-pt) doğrusal konvolüsyon + F0-uyarlamalı gerçek zarf (True-Envelope) kepstral çıkarımı uygular.',
    fixEn:
      'v3.0 applies 2× zero-padded (2048-pt) linear convolution with F0-adaptive True-Envelope cepstral extraction.',
  },
  {
    num: '02',
    module: 'HybridRegenerator.h',
    titleTr: 'Çift Türevlenmiş LF Glottal Tablosu ve Yanlış PSOLA Kazancı',
    titleEn: 'Double-Differentiated LF Table & Mis-Scaled PSOLA Gain',
    issueTr:
      'LFGlottalTable zaten türevlenmiş akış dalgasını üretirken Stage 2 sonlu farkla ikinci kez türev alıp tiz cızırtı üretiyor; üstüne √(tOut/max(tIn,tOut)) faktörü yukarı perde kaymalarında seviye dengesizliği yaratıyordu.',
    issueEn:
      'Stage 2 differentiated the already-differentiated LF table a second time, turning -12 dB/oct warmth into harsh buzz while mis-scaling PSOLA overlap energy.',
    fixTr:
      'v3.0, sıfır-DC dengeli tek türev LF tablosu ve uyumlu √(tOut/tIn) PSOLA enerji normalizasyonu kullanır.',
    fixEn:
      'v3.0 uses a DC-balanced single-derivative LF table with coherent √(tOut/tIn) PSOLA energy compensation.',
  },
  {
    num: '03',
    module: 'PluginProcessor.cpp',
    titleTr: 'Giriş Ön Koşullandırma Eksikliği (50 Hz Uğultu Çökmesi)',
    titleEn: 'Unconditioned Input Feeding Pitch Tracker & GCI',
    issueTr:
      'Ham mikrofon sinyali yüksek geçiren filtre (HPF) olmadan doğrudan YIN ve 650 Hz GCI dedektörüne gidiyordu; masa darbesi veya nefes patlaması perde izleyiciyi 50 Hz civarına kilitliyordu.',
    issueEn:
      'Raw mic audio hit YIN and the 650 Hz GCI peak picker without HPF, letting <60 Hz desk rumble hijack pitch tracking.',
    fixTr:
      'v3.0 zincir başına 2. derece Butterworth HPF (40–160 Hz), yumuşak gürültü kapısı ve dinamik De-Esser ekler.',
    fixEn:
      'v3.0 inserts a pre-DSP 2nd-order Butterworth HPF (40–160 Hz), soft noise gate, and dynamic De-Esser.',
  },
  {
    num: '04',
    module: 'HybridRegenerator.h',
    titleTr: 'Ünsüz Koruma (Consonant Guard) Geçiş Çıtırtıları',
    titleEn: 'Consonant Guard Sibilant Smearing & CV Clicks',
    issueTr:
      'v2.0 yalnızca formant oranını %25’e düşürüyor, s/ş/t/k ünsüzlerini yine perde kaydırma hattından geçirip sert geçişlerde tıklama oluşturuyordu.',
    issueEn:
      'v2.0 only attenuated formant warp to 25% while still pushing unvoiced fricatives through Stage 1–3 with abrupt transitions.',
    fixTr:
      'v3.0, HF/LF enerji oranı + sıfır-geçiş hızı (ZCR) ile ünsüzleri pürüzsüz çapraz geçişle perde kaydırıcıdan ayırır.',
    fixEn:
      'v3.0 isolates unvoiced sibilants/plosives via HF/LF + ZCR cues and crossfades them around pitch shifting cleanly.',
  },
  {
    num: '05',
    module: 'AutoTune.h',
    titleTr: 'Auto-Tune 90 ms Melisma Gecikmesi ve Kilit Eksikliği',
    titleEn: 'Auto-Tune 90 ms Melisma Lag & Missing Key/Speed Controls',
    issueTr:
      'Sabit 90 ms nota-merkezi filtresi hızlı geçişlerde notayı geriden takip ediyor; ayrıca kesirli perde kaydırmasında tam sayı yuvarlaması gam kayması yapıyordu.',
    issueEn:
      'A fixed 90 ms note-center filter lagged on vocal runs, and integer-rounded shift keys mismatched fractional pitch offsets.',
    fixTr:
      'v3.0 hızlı sıçrama algılayıcısı (>0.68 st), bağımsız Retune Hızı (ms), Vibrato Koruma ve Manuel Gam Kilidi (Majör/Minör/Hicaz/Pentatonik) sunar.',
    fixEn:
      'v3.0 adds fast onset reset (>0.68 st), independent Retune Speed (ms), Vibrato Preserve, and manual Key/Scale lock.',
  },
  {
    num: '06',
    module: 'PluginProcessor.cpp',
    titleTr: '0 ms Sert Limiter Kırpması ve Eşiksiz Seviye Pompalaması',
    titleEn: '0 ms Hard-Clip Limiter & Ungated Loudness Pumping',
    issueTr:
      'v2.0 piklerde 0 ms anlık kazanç kesintisiyle harmonik bozulma yaratıyor, eşiksiz RMS seviye eşleme ise nefes aralarında dip gürültüyü pompalıyordu.',
    issueEn:
      'v2.0 hard-clamped peaks at 0 ms attack causing crunch, while ungated broadband RMS matching pumped breath pauses.',
    fixTr:
      'v3.0, 3 ms lookahead yumuşak-dizli −0,3 dBFS True-Peak Limiter (canlı GR göstergeli) ve −48 dBFS eşikli K-ağırlıklı seviye eşleme uygular.',
    fixEn:
      'v3.0 deploys a 3 ms lookahead soft-knee -0.3 dBFS limiter with GR metering and -48 dBFS gated K-weighted loudness matching.',
  },
];

export const EngineeringAuditView: React.FC<EngineeringAuditViewProps> = ({ lang }) => {
  const [results, setResults] = useState<DiagnosticCheckResult[] | null>(null);

  const handleRunTests = () => {
    setResults(runEngineeringDiagnostics(48000));
  };

  const passCount = results ? results.filter((r) => r.passed).length : 0;

  return (
    <div className="space-y-5">
      {/* Top Action Strip: Live DSP Diagnostic Bench */}
      <section className="rounded-xl border border-white/10 bg-[#0B0F17] p-5">
        <div className="flex flex-wrap items-center justify-between gap-4">
          <div className="max-w-2xl">
            <h2 className="text-base font-bold text-slate-100">
              {lang === 'tr'
                ? 'Ses Mühendisliği DSP Doğrulama Tezgâhı (48 kHz)'
                : 'Audio Engineering DSP Self-Test Bench (48 kHz)'}
            </h2>
            <p className="mt-1 text-xs leading-relaxed text-slate-400">
              {lang === 'tr'
                ? 'AlptugDUB 2.0 kod tabanında tespit edilen 6 temel DSP kusurunun v3.0 motorunda giderildiğini deterministik test sinyalleriyle canlı ölçün.'
                : 'Verify all 6 DSP defects diagnosed in AlptugDUB 2.0 against deterministic test signals in the rebuilt v3.0 engine.'}
            </p>
          </div>

          <button
            type="button"
            onClick={handleRunTests}
            className="inline-flex items-center gap-2 rounded-lg bg-amber-500 px-4 py-2.5 text-xs font-semibold text-slate-950 hover:bg-amber-400 transition-colors whitespace-nowrap shrink-0"
          >
            <Play className="h-3.5 w-3.5 fill-current" />
            <span>{lang === 'tr' ? 'DSP Testlerini Çalıştır' : 'Run DSP Benchmarks'}</span>
          </button>
        </div>

        {results && (
          <div className="mt-5 border-t border-white/10 pt-4">
            <div className="mb-3 flex items-center justify-between text-xs">
              <span className="font-semibold text-emerald-400">
                {lang === 'tr'
                  ? `Ölçüm Tamamlandı · ${passCount} / ${results.length} test başarıyla geçti`
                  : `Benchmark Complete · ${passCount} / ${results.length} checks passed`}
              </span>
              <span className="font-mono text-slate-500">48,000 Hz · 32-bit Float</span>
            </div>

            <div className="overflow-x-auto">
              <table className="w-full text-xs">
                <thead>
                  <tr className="border-b border-white/10 text-left text-slate-500">
                    <th className="pb-2 pr-3 font-medium">{lang === 'tr' ? 'Test' : 'Check'}</th>
                    <th className="pb-2 pr-3 font-medium">{lang === 'tr' ? 'Ölçülen Değer' : 'Measured'}</th>
                    <th className="pb-2 pr-3 font-medium">{lang === 'tr' ? 'Hedef Tolerans' : 'Target'}</th>
                    <th className="pb-2 font-medium text-right">{lang === 'tr' ? 'Sonuç' : 'Status'}</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-white/5">
                  {results.map((r) => (
                    <tr key={r.id}>
                      <td className="py-2.5 pr-3">
                        <div className="font-semibold text-slate-200">
                          {lang === 'tr' ? r.titleTr : r.titleEn}
                        </div>
                        <div className="text-[11px] text-slate-400">
                          {lang === 'tr' ? r.defectFixedTr : r.defectFixedEn}
                        </div>
                      </td>
                      <td className="py-2.5 pr-3 font-mono tabular-nums text-amber-300 whitespace-nowrap">
                        {r.measured}
                      </td>
                      <td className="py-2.5 pr-3 font-mono tabular-nums text-slate-400 whitespace-nowrap">
                        {r.target}
                      </td>
                      <td className="py-2.5 text-right whitespace-nowrap">
                        <span
                          className={`inline-flex items-center gap-1 font-mono font-semibold ${
                            r.passed ? 'text-emerald-400' : 'text-rose-400'
                          }`}
                        >
                          {r.passed ? (
                            <CheckCircle2 className="h-3.5 w-3.5" />
                          ) : (
                            <ShieldAlert className="h-3.5 w-3.5" />
                          )}
                          {r.passed ? (lang === 'tr' ? 'GEÇTİ' : 'PASS') : lang === 'tr' ? 'KALDI' : 'FAIL'}
                        </span>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        )}
      </section>

      {/* 6 Diagnosed Architectural Fixes Grid */}
      <section aria-label={lang === 'tr' ? 'Tespit Edilen DSP Kusurları ve Çözümleri' : 'Diagnosed DSP Defects & Fixes'}>
        <div className="grid grid-cols-1 gap-3.5 md:grid-cols-2">
          {AUDIT_DEFECTS.map((item) => (
            <article
              key={item.num}
              className="rounded-xl border border-white/10 bg-[#0D121B] p-4 flex flex-col justify-between"
            >
              <div>
                <div className="flex items-center justify-between gap-2 text-xs text-slate-400">
                  <span className="font-mono font-semibold text-amber-400">{item.num}.</span>
                  <span className="font-mono text-[11px] text-slate-500">{item.module}</span>
                </div>
                <h3 className="mt-1.5 text-sm font-bold text-slate-100">
                  {lang === 'tr' ? item.titleTr : item.titleEn}
                </h3>
                <p className="mt-2 text-xs leading-relaxed text-slate-400">
                  <strong className="font-semibold text-rose-300">
                    {lang === 'tr' ? 'Eski Kusur (v2.0): ' : 'v2.0 Defect: '}
                  </strong>
                  {lang === 'tr' ? item.issueTr : item.issueEn}
                </p>
              </div>
              <p className="mt-2.5 border-t border-white/8 pt-2.5 text-xs leading-relaxed text-slate-300">
                <strong className="font-semibold text-cyan-300">
                  {lang === 'tr' ? 'Yeni Mimari (v3.0): ' : 'v3.0 Rebuild: '}
                </strong>
                {lang === 'tr' ? item.fixTr : item.fixEn}
              </p>
            </article>
          ))}
        </div>
      </section>
    </div>
  );
};
