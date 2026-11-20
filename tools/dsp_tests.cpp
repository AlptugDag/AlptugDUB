// AlptugDUB 3.0 — deterministic C++20 DSP engineering verification suite.
// Verifies all 6 v3.0 DSP architectural fixes + 2x zero-padded linear OLA stability.
#include "dsp/AutoTune.h"
#include "dsp/HybridRegenerator.h"
#include "dsp/PitchTracker.h"
#include "dsp/StudioDynamics.h"
#include "dsp/VocalTractWarper.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(const char* name, bool ok, const char* detail) {
    if (ok) {
        std::printf("[PASS] %s — %s\n", name, detail);
    } else {
        std::printf("[FAIL] %s — %s\n", name, detail);
        ++failures;
    }
}

void testHpfAndYinRumbleRejection() {
    constexpr double sr = 48000.0;
    constexpr double targetHz = 146.83; // D3
    adub::PitchTracker yin;
    yin.prepare(sr, 80.0);

    adub::PitchEstimate last{};
    for (int i = 0; i < 4800; ++i) {
        const double t = (double) i / sr;
        const float rumble = (float) (0.65 * std::sin(2.0 * M_PI * 42.0 * t));
        const float voice = (float) (0.50 * std::sin(2.0 * M_PI * targetHz * t) +
                                     0.32 * std::sin(2.0 * M_PI * targetHz * 2.0 * t) +
                                     0.18 * std::sin(2.0 * M_PI * targetHz * 3.0 * t));
        yin.push(rumble + voice);
        if (i > 3000 && i % 240 == 0) last = yin.estimate();
    }
    const float f0 = last.voiced() ? (float) (sr / last.period) : 0.0f;
    const float err = std::abs(f0 - (float) targetHz);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "measured %.2f Hz (err %.2f Hz, conf %.2f)", f0, err, last.confidence);
    check("1. Input HPF + YIN rumble rejection", last.voiced() && err < 1.5f, buf);
}

void testLfGlottalSingleDerivativeBalance() {
    adub::LFGlottalTable lf;
    lf.prepare();
    constexpr int N = 1024;
    double maxDc = 0.0, worstRmsErr = 0.0;
    for (float g : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        double sum = 0.0, sq = 0.0;
        for (int i = 0; i < N; ++i) {
            const float s = lf.sample((float) i / (float) N, g, 150.0f / 48000.0f);
            sum += s;
            sq += (double) s * s;
        }
        maxDc = std::max(maxDc, std::abs(sum / N));
        worstRmsErr = std::max(worstRmsErr, std::abs(std::sqrt(sq / N) - 1.0));
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "max |DC|=%.6f, max |RMS-1|=%.4f", maxDc, worstRmsErr);
    check("2a. LF Glottal single-derivative DC balance & unit RMS", maxDc < 0.002 && worstRmsErr < 0.08, buf);
}

void testPsolaEnergyNormalization() {
    const float upOct = adub::HybridRegenerator::computePsolaGainCompensation(320.0f, 160.0f);   // +12 st
    const float unison = adub::HybridRegenerator::computePsolaGainCompensation(320.0f, 320.0f);  // 0 st
    const float downOct = adub::HybridRegenerator::computePsolaGainCompensation(320.0f, 640.0f); // -12 st
    char buf[128];
    std::snprintf(buf, sizeof(buf), "+12st=%.3f, 0st=%.3f, -12st=%.3f", upOct, unison, downOct);
    check("2b. Coherent PSOLA energy normalization",
          std::abs(upOct - std::sqrt(0.5f)) < 0.02f && std::abs(unison - 1.0f) < 0.001f &&
              std::abs(downOct - std::sqrt(2.0f)) < 0.02f,
          buf);
}

void testTrueEnvelopeAndFormantWarp() {
    adub::VocalTractWarper warper;
    warper.prepare(48000.0);

    bool monotonic = true;
    float prev = -1.0f;
    for (float f = 100.0f; f <= 8000.0f; f += 120.0f) {
        const float w = adub::VocalTractWarper::warpFormantFreq(f, -6.5f);
        if (w <= prev) monotonic = false;
        prev = w;
    }
    const float f1Shift = adub::VocalTractWarper::warpFormantFreq(500.0f, -4.0f);
    const float f3Shift = adub::VocalTractWarper::warpFormantFreq(2800.0f, -4.0f);

    // Build synthetic harmonic spectrum with bass F0=85 Hz and sharp F1=500 Hz & F2=1500 Hz peaks
    const int bins = warper.numBins();
    const float binHz = warper.hzPerBin();
    std::vector<float> mag((size_t) bins, 1e-4f);
    for (int h = 1; h <= 45; ++h) {
        const float fh = 85.0f * (float) h;
        const int k = (int) std::lround(fh / binHz);
        if (k <= 0 || k >= bins) continue;
        const float f1 = 1.0f / (1.0f + std::pow((fh - 500.0f) / 70.0f, 2.0f));
        const float f2 = 0.7f / (1.0f + std::pow((fh - 1500.0f) / 110.0f, 2.0f));
        mag[(size_t) k] = 0.05f + 1.5f * f1 + 1.2f * f2;
    }
    std::vector<float> env((size_t) bins, 0.0f), tract((size_t) bins, 0.0f), tilt((size_t) bins, 0.0f);
    warper.extractEnvelope(mag.data(), 85.0f, env.data(), tract.data(), tilt.data());

    const int k500 = (int) std::lround(500.0f / binHz);
    const int k1000 = (int) std::lround(1000.0f / binHz);
    const int k1500 = (int) std::lround(1500.0f / binHz);
    const bool peaksPreserved = env[(size_t) k500] > env[(size_t) k1000] * 2.0f &&
                                env[(size_t) k1500] > env[(size_t) k1000] * 1.5f;

    char buf[160];
    std::snprintf(buf, sizeof(buf), "fft=%d, F1(500)->%.0f Hz, F3(2800)->%.0f Hz, env@500=%.2f vs @1000=%.2f vs @1500=%.2f",
                  warper.fftLength(), f1Shift, f3Shift, env[(size_t) k500], env[(size_t) k1000], env[(size_t) k1500]);
    check("3. 2x Zero-Padded True-Envelope & Monotonic Formant Warp",
          warper.fftLength() == 2048 && monotonic && f1Shift < 420.0f && f3Shift > 2100.0f && peaksPreserved, buf);
}

void testConsonantGuardIsolation() {
    const auto sibilant = adub::HybridRegenerator::scoreConsonantCues(0.08f, 3.8f, 5400.0f, 0.08f);
    const auto vowel = adub::HybridRegenerator::scoreConsonantCues(0.18f, 0.06f, 420.0f, 0.94f);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "sibilant prob=%.2f, vowel prob=%.2f", sibilant.consonantProb, vowel.consonantProb);
    check("4. Multi-cue Consonant Guard separation", sibilant.consonantProb > 0.85f && vowel.consonantProb < 0.05f, buf);
}

void testAutoTuneMelismaAndScaleLock() {
    adub::AutoTune at;
    at.prepare(48000.0);

    adub::AutoTune::Settings s;
    s.amount = 1.0f;
    s.retuneSpeedMs = 12.0f;
    s.vibratoPreserve = 0.0f;
    s.scaleMode = adub::AutoTune::ScaleMode::Major;
    s.manualKey = 9; // A Major

    adub::AutoTune::Result res{};
    for (int step = 0; step < 12; ++step) {
        // 432 Hz is ~31.8 cents flat of A4 (440 Hz)
        res = at.update(432.0f, 0.92f, 0.0f, s, adub::VoiceType::Tenor, 0.01f);
    }
    const float centsA4 = res.correctionSemitones * 100.0f;

    // Test Hijaz scale lock in D (D=2): D#, F#, G, A, A#, C -> Eb (63) should be in scale, E (64) should snap to D# (63) or F# (66)
    const float hijazEb = adub::AutoTune::nearestScaleMidi(63.1f, 2, adub::AutoTune::ScaleType::Hijaz);
    const float hijazFs = adub::AutoTune::nearestScaleMidi(65.8f, 2, adub::AutoTune::ScaleType::Hijaz);

    // Test melisma fast jump from A4 (440 Hz) to C#5 (554.37 Hz) — target should switch immediately on the jump frame!
    const auto jumpRes = at.update(554.37f, 0.94f, 0.0f, s, adub::VoiceType::Tenor, 0.01f);

    char buf[160];
    std::snprintf(buf, sizeof(buf), "432->440 corr=+%.1f c (target +31.8 c), Hijaz D=(%.0f,%.0f), jumpTarget=%.0f",
                  centsA4, hijazEb, hijazFs, jumpRes.targetMidi);
    check("5. Auto-Tune scale lock, Hijaz & melisma fast onset",
          centsA4 >= 28.0f && centsA4 <= 35.0f && hijazEb == 63.0f && hijazFs == 66.0f && jumpRes.targetMidi == 73.0f, buf);
}

void testLookaheadLimiterAndKWeighting() {
    constexpr double sr = 48000.0;
    adub::LookaheadLimiter lim;
    lim.prepare(sr, 2, -0.3f, 3.0f);

    float maxOut = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        const float hot = (float) (2.4 * std::sin(2.0 * M_PI * 220.0 * (double) i / sr)); // +7.6 dBFS overload
        float frame[2] = {hot, -hot};
        const float pk = lim.processFrame(frame, 2);
        maxOut = std::max(maxOut, pk);
    }
    const float peakDb = 20.0f * std::log10(std::max(1e-6f, maxOut));
    const float grDb = lim.consumeMaxGrDb();

    // K-weighted loudness matcher gate test: silence (-70 dBFS) should leave gain untouched at 1.0
    adub::KWeightedLoudnessMatcher kw;
    kw.prepare(sr);
    for (int i = 0; i < 4800; ++i) {
        const float quiet = (float) (3e-4 * std::sin(2.0 * M_PI * 200.0 * (double) i / sr));
        kw.step(quiet, quiet * 0.25f);
    }
    const float gatedGain = kw.currentGain();

    // Active speech at -14 dBFS with wet at -20 dBFS (0.5x) should lift makeup gain smoothly toward ~2.0x
    for (int i = 0; i < 48000; ++i) {
        const float active = (float) (0.20 * std::sin(2.0 * M_PI * 440.0 * (double) i / sr));
        kw.step(active, active * 0.5f);
    }
    const float matchedGain = kw.currentGain();

    char buf[160];
    std::snprintf(buf, sizeof(buf), "peak=%.2f dBFS, GR=%.1f dB, lat=%d samp, gateG=%.2f, matchG=%.2f",
                  peakDb, grDb, lim.latencySamples(), gatedGain, matchedGain);
    check("6. 3 ms Lookahead -0.3 dBFS Limiter & K-Weighted Gated Loudness",
          peakDb <= -0.28f && grDb < -6.0f && lim.latencySamples() == 144 && std::abs(gatedGain - 1.0f) < 0.01f &&
              matchedGain > 1.70f && matchedGain <= 2.20f,
          buf);
}

void testHybridRegeneratorLinearOlaPassthrough() {
    constexpr double sr = 48000.0;
    constexpr int total = 24000;
    constexpr int blk = 256;

    adub::HybridRegenerator reg;
    reg.prepare(sr, 2, blk);
    const int lat = reg.latencySamples();

    adub::HybridParams p;
    p.pitchSemitones = 0.0f;
    p.formantSemitones = 0.0f;
    p.regen = 0.0f;
    p.glottal = 0.0f;
    p.growl = 0.0f;
    p.breath = 0.0f;
    p.deEsser = 0.0f;
    p.characterIndex = 0;
    p.consonantGuard = true;

    std::vector<float> inL((size_t) total), inR((size_t) total);
    std::vector<float> outL((size_t) total), outR((size_t) total);
    for (int i = 0; i < total; ++i) {
        const double t = (double) i / sr;
        const float v = (float) (0.25 * std::sin(2.0 * M_PI * 220.0 * t) + 0.15 * std::sin(2.0 * M_PI * 660.0 * t));
        inL[(size_t) i] = v;
        inR[(size_t) i] = v;
    }

    for (int pos = 0; pos < total; pos += blk) {
        const int n = std::min(blk, total - pos);
        const float* inPtrs[2] = {inL.data() + pos, inR.data() + pos};
        float* outPtrs[2] = {outL.data() + pos, outR.data() + pos};
        reg.process(inPtrs, outPtrs, n, p, false);
    }

    bool finite = true;
    double inSq = 0.0, outSq = 0.0;
    for (int i = lat + 2048; i < total - 1024; ++i) {
        if (!std::isfinite(outL[(size_t) i]) || !std::isfinite(outR[(size_t) i])) finite = false;
        inSq += (double) inL[(size_t) (i - lat)] * inL[(size_t) (i - lat)];
        outSq += (double) outL[(size_t) i] * outL[(size_t) i];
    }
    const double ratio = std::sqrt(outSq / std::max(1e-12, inSq));
    char buf[128];
    std::snprintf(buf, sizeof(buf), "finite=%s, RMS ratio=%.3f, latency=%d samp", finite ? "yes" : "NO", ratio, lat);
    check("7. HybridRegenerator 2x zero-padded linear OLA stability", finite && ratio > 0.85 && ratio < 1.15, buf);
}

} // namespace

int main() {
    std::printf("=== AlptugDUB 3.0.0 C++20 DSP Verification Suite ===\n");
    testHpfAndYinRumbleRejection();
    testLfGlottalSingleDerivativeBalance();
    testPsolaEnergyNormalization();
    testTrueEnvelopeAndFormantWarp();
    testConsonantGuardIsolation();
    testAutoTuneMelismaAndScaleLock();
    testLookaheadLimiterAndKWeighting();
    testHybridRegeneratorLinearOlaPassthrough();
    std::printf("=== %s (%d failures) ===\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
