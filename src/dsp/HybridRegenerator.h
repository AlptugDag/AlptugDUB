// AlptugDUB by Alptuğ Dağ — hybrid glottal regeneration, neural Bark timbre shaping & consonant guard.
#pragma once

#include "PitchTracker.h"
#include "VocalTractWarper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace adub {

/** Parameters controlling one frame/block of the hybrid regeneration engine. */
struct HybridParams {
    float pitchSemitones = 0.0f;
    float formantSemitones = 0.0f;
    float regen = 0.75f;           // 0 = warped spectral only, 1 = full glottal regeneration
    float glottal = 0.0f;          // -1 pressed chest .. +1 soft/airy head voice (LF Oq)
    float growl = 0.0f;            // 0..1 subharmonic F0/2, F0/3 & diplophonia
    float breath = 0.10f;          // 0..1 pitch-synchronous formant-shaped aspiration
    float age = 0.0f;              // -50 young .. +50 old (tremor & micro-jitter)
    float nasal = 0.0f;            // 0..1 nasal resonance coupling
    float effort = 1.0f;           // instantaneous vocal effort ratio (~0.4..2.5)
    float speakerMedianF0 = 0.0f;  // learned speaker median F0 (Hz)
    int characterIndex = 0;        // 0..12 character archetype
    bool consonantGuard = true;    // preserve sibilants & plosives unshifted
    bool steadyPitch = false;      // smooth pitch wobbles in regenerated source
};

/** Live telemetry reported back to the processor / UI. */
struct HybridStatus {
    float voiced = 0.0f;     // 0..1 instantaneous voiced probability
    float consonant = 0.0f;  // 0..1 unvoiced/plosive guard activity
    float regen = 0.0f;      // 0..1 active glottal regeneration level
    float subharmonic = 0.0f;
};

/**
 * Timestamped ring buffer storing pitch/voicing frames so that delayed audio streams can look up
 * the exact sample-aligned F0 and confidence at any STFT frame centre.
 */
class PitchTimeline {
public:
    struct Entry {
        int64_t time = -1;
        float f0 = 0.0f;         // Hz (kept even slightly below voicing threshold for smooth tails)
        float confidence = 0.0f; // 0..1
        float aperiodicity = 1.0f;
        float rms = 0.0f;
    };

    void reset() {
        entries.fill(Entry{});
        writeIdx = 0;
        count = 0;
    }

    void push(int64_t time, float f0, float confidence, float aperiodicity, float rms) {
        entries[(size_t) (writeIdx & kMask)] = {time, f0, confidence, aperiodicity, rms};
        ++writeIdx;
        count = std::min(count + 1, kSize);
    }

    Entry query(int64_t targetTime) const {
        if (count == 0) return {};
        const int newestOff = count - 1;
        const Entry* prev = nullptr;
        const Entry* next = nullptr;
        for (int i = 0; i < count; ++i) {
            const int idx = (writeIdx - 1 - i) & kMask;
            const Entry& e = entries[(size_t) idx];
            if (e.time <= targetTime) {
                prev = &e;
                if (i > 0) next = &entries[(size_t) ((writeIdx - i) & kMask)];
                break;
            }
            if (i == newestOff) next = &e;
        }
        if (prev && next && next->time > prev->time) {
            const float alpha = std::clamp((float) (targetTime - prev->time) / (float) (next->time - prev->time), 0.0f, 1.0f);
            Entry out;
            out.time = targetTime;
            out.confidence = prev->confidence + (next->confidence - prev->confidence) * alpha;
            out.aperiodicity = prev->aperiodicity + (next->aperiodicity - prev->aperiodicity) * alpha;
            out.rms = prev->rms + (next->rms - prev->rms) * alpha;
            if (prev->f0 > 40.0f && next->f0 > 40.0f) {
                const float lf = std::log(prev->f0) + (std::log(next->f0) - std::log(prev->f0)) * alpha;
                out.f0 = std::exp(lf);
            } else {
                out.f0 = prev->f0 > 40.0f ? prev->f0 : next->f0;
            }
            return out;
        }
        if (prev) return *prev;
        if (next) return *next;
        return {};
    }

private:
    static constexpr int kSize = 1024;
    static constexpr int kMask = kSize - 1;
    std::array<Entry, kSize> entries{};
    int writeIdx = 0, count = 0;
};

/**
 * Band-limited Liljencrants-Fant (LF) glottal flow derivative wavetable with 8 open-quotient
 * shapes (chest-pressed Oq=0.32 to airy-head Oq=0.82) and 6 octave mipmaps to eliminate aliasing.
 */
class LFGlottalTable {
public:
    static constexpr int kOqSteps = 8;
    static constexpr int kMips = 6;
    static constexpr int kLen = 1024;

    void prepare() {
        if (ready) return;
        std::array<std::complex<float>, kLen> spec{};
        std::array<std::complex<float>, kLen> twFwd{}, twInv{};
        for (int k = 0; k < kLen / 2; ++k) {
            const float ang = (float) (2.0 * M_PI * k / kLen);
            twFwd[(size_t) k] = {std::cos(ang), -std::sin(ang)};
            twInv[(size_t) k] = {std::cos(ang), std::sin(ang)};
        }
        auto runFft = [&](std::array<std::complex<float>, kLen>& a, bool inv) {
            for (int i = 1, j = 0; i < kLen; ++i) {
                int bit = kLen >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap(a[(size_t) i], a[(size_t) j]);
            }
            const auto& tw = inv ? twInv : twFwd;
            for (int len = 2; len <= kLen; len <<= 1) {
                const int step = kLen / len;
                for (int i = 0; i < kLen; i += len) {
                    for (int j = 0; j < len / 2; ++j) {
                        const auto u = a[(size_t) (i + j)];
                        const auto v = a[(size_t) (i + j + len / 2)] * tw[(size_t) (j * step)];
                        a[(size_t) (i + j)] = u + v;
                        a[(size_t) (i + j + len / 2)] = u - v;
                    }
                }
            }
            if (inv) {
                for (auto& x : a) x /= (float) kLen;
            }
        };

        for (int q = 0; q < kOqSteps; ++q) {
            const float uq = (float) q / (float) (kOqSteps - 1);
            const float oq = 0.32f + 0.50f * uq; // 0.32 pressed chest .. 0.82 soft head
            const float te = oq;
            const float tp = te * (0.64f + 0.06f * uq);
            const float ta = 0.010f + 0.085f * uq;
            const float eps = 1.0f / ta;

            // Construct positive opening lobe and negative closing excitation, DC-balanced
            std::array<float, kLen> raw{};
            double posSum = 0.0, negSum = 0.0;
            for (int i = 0; i < kLen; ++i) {
                const float t = (float) i / (float) kLen;
                float val = 0.0f;
                if (t < tp) {
                    val = std::sin((float) M_PI * t / tp);
                    posSum += val;
                } else if (t < te) {
                    const float u = (t - tp) / std::max(1e-4f, te - tp);
                    val = -std::sin((float) (M_PI * 0.5) * u) * std::exp(1.2f * u);
                    negSum += val;
                } else {
                    const float endVal = -std::exp(1.2f);
                    const float dt = t - te;
                    const float rem = 1.0f - te;
                    const float decay = (std::exp(-eps * dt) - std::exp(-eps * rem)) / std::max(1e-5f, 1.0f - std::exp(-eps * rem));
                    val = endVal * std::max(0.0f, decay);
                    negSum += val;
                }
                raw[(size_t) i] = val;
            }
            const float posScale = posSum > 1e-6 ? (float) (-negSum / posSum) : 1.0f;
            for (int i = 0; i < kLen; ++i) {
                const float t = (float) i / (float) kLen;
                if (t < tp) raw[(size_t) i] *= posScale;
                spec[(size_t) i] = {raw[(size_t) i], 0.0f};
            }
            runFft(spec, false);

            for (int m = 0; m < kMips; ++m) {
                const int maxHarm = std::min(kLen / 2 - 1, 512 >> m);
                const int taperLo = std::max(4, (maxHarm * 85) / 100);
                std::array<std::complex<float>, kLen> band{};
                // Zero DC (bin 0)
                for (int k = 1; k <= maxHarm; ++k) {
                    float w = 1.0f;
                    if (k > taperLo) {
                        const float u = (float) (k - taperLo) / (float) std::max(1, maxHarm - taperLo);
                        w = 0.5f * (1.0f + std::cos((float) M_PI * u));
                    }
                    band[(size_t) k] = spec[(size_t) k] * w;
                    band[(size_t) (kLen - k)] = spec[(size_t) (kLen - k)] * w;
                }
                runFft(band, true);
                double sq = 0.0;
                for (int i = 0; i < kLen; ++i) sq += (double) band[(size_t) i].real() * band[(size_t) i].real();
                const float invRms = sq > 1e-12 ? (float) (1.0 / std::sqrt(sq / kLen)) : 1.0f;
                for (int i = 0; i < kLen; ++i) tables[(size_t) q][(size_t) m][(size_t) i] = band[(size_t) i].real() * invRms;
            }
        }
        ready = true;
    }

    /** Looks up band-limited LF glottal waveform for phase in [0,1), glottal in [-1,1], and pitch f0/sr. */
    float sample(float phase, float glottal, float f0Norm) const {
        const float qPos = std::clamp((glottal + 1.0f) * 0.5f, 0.0f, 1.0f) * (kOqSteps - 1);
        const int q0 = std::min(kOqSteps - 2, (int) qPos);
        const float qf = qPos - (float) q0;

        const float maxAllowedHarm = std::clamp(0.45f / std::max(1e-4f, f0Norm), 16.0f, 512.0f);
        const float mipPos = std::clamp(std::log2(512.0f / maxAllowedHarm), 0.0f, (float) (kMips - 1));
        const int m0 = std::min(kMips - 2, (int) mipPos);
        const float mf = mipPos - (float) m0;

        const float p = (phase - std::floor(phase)) * kLen;
        const int i0 = ((int) p) & (kLen - 1);
        const int i1 = (i0 + 1) & (kLen - 1);
        const float pf = p - std::floor(p);

        auto at = [&](int q, int m) {
            const auto& tbl = tables[(size_t) q][(size_t) m];
            return tbl[(size_t) i0] + (tbl[(size_t) i1] - tbl[(size_t) i0]) * pf;
        };
        const float v0 = at(q0, m0) * (1.0f - mf) + at(q0, m0 + 1) * mf;
        const float v1 = at(q0 + 1, m0) * (1.0f - mf) + at(q0 + 1, m0 + 1) * mf;
        return v0 + (v1 - v0) * qf;
    }

private:
    bool ready = false;
    std::array<std::array<std::array<float, kLen>, kMips>, kOqSteps> tables{};
};

/**
 * Embedded 24-Bark-band -> 32-hidden-unit MLP + 8-D character embedding neural timbre shaper.
 * Normalizes the speaker's learned spectral coloration toward target voice archetypes with smooth,
 * level-dependent non-linear saturation (tanh) and Bark-to-FFT spectral gain interpolation.
 */
class NeuralTimbreShaper {
public:
    static constexpr int kBands = 24;
    static constexpr int kHidden = 32;
    static constexpr int kEmbed = 8;

    void prepare(double sampleRate, int fftSize) {
        sr = sampleRate;
        halfBins = fftSize / 2 + 1;
        const float binHz = (float) (sampleRate / fftSize);
        static constexpr std::array<float, kBands + 1> edges = {
            20,   100,  200,  300,  400,  510,  630,  770,  920,  1080, 1270, 1480, 1720,
            2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500};
        for (int b = 0; b < kBands; ++b) {
            const float fLo = edges[(size_t) b], fHi = std::min((float) (sampleRate * 0.48), edges[(size_t) (b + 1)]);
            bandLo[(size_t) b] = std::clamp((int) std::floor(fLo / binHz), 1, halfBins - 1);
            bandHi[(size_t) b] = std::clamp((int) std::ceil(fHi / binHz), bandLo[(size_t) b], halfBins - 1);
            bandCentreBin[(size_t) b] = 0.5f * (edges[(size_t) b] + edges[(size_t) (b + 1)]) / binHz;
        }
        speakerBark.fill(0.0f);
        speakerFrames = 0;
        buildWeights();
    }

    void resetSpeaker() {
        speakerBark.fill(0.0f);
        speakerFrames = 0;
    }

    /** Updates long-term speaker Bark profile on clear voiced frames. */
    void learnSpeaker(const float* env, float voicedMask) {
        if (voicedMask < 0.65f) return;
        std::array<float, kBands> cur{};
        double mean = 0.0;
        for (int b = 0; b < kBands; ++b) {
            double sum = 0.0;
            int cnt = 0;
            for (int k = bandLo[(size_t) b]; k <= bandHi[(size_t) b]; ++k) {
                sum += std::log(std::max(1e-7f, env[k]));
                ++cnt;
            }
            cur[(size_t) b] = (float) (sum / std::max(1, cnt));
            mean += cur[(size_t) b];
        }
        mean /= kBands;
        const float rate = speakerFrames < 40 ? 0.15f : 0.008f;
        for (int b = 0; b < kBands; ++b) speakerBark[(size_t) b] += ((cur[(size_t) b] - (float) mean) - speakerBark[(size_t) b]) * rate;
        ++speakerFrames;
    }

    /** Computes per-bin neural timbre multiplier `outGain[0..halfBins-1]` for the current frame. */
    void evaluate(const float* warpedEnv, const HybridParams& p, float* outGain, float& outHnrBias) const {
        // Extract 24-Bark log profile relative to frame mean
        std::array<float, kBands> frameBark{};
        double mean = 0.0;
        for (int b = 0; b < kBands; ++b) {
            double sum = 0.0;
            int cnt = 0;
            for (int k = bandLo[(size_t) b]; k <= bandHi[(size_t) b]; ++k) {
                sum += std::log(std::max(1e-7f, warpedEnv[k]));
                ++cnt;
            }
            frameBark[(size_t) b] = (float) (sum / std::max(1, cnt));
            mean += frameBark[(size_t) b];
        }
        mean /= kBands;

        // Input features: normalized warped Bark energy minus learned speaker deviation
        std::array<float, kBands + kEmbed> in{};
        const float spkWeight = std::min(1.0f, (float) speakerFrames / 60.0f) * 0.45f;
        for (int b = 0; b < kBands; ++b) {
            const float normFrame = (frameBark[(size_t) b] - (float) mean) * 0.35f;
            const float dev = (speakerBark[(size_t) b] - neutralRefBark[(size_t) b]) * spkWeight * 0.35f;
            in[(size_t) b] = std::clamp(normFrame - dev, -2.5f, 2.5f);
        }
        const auto emb = buildEmbedding(p);
        for (int e = 0; e < kEmbed; ++e) in[(size_t) (kBands + e)] = emb[(size_t) e];

        // Hidden layer: 32 tanh neurons
        std::array<float, kHidden> hid{};
        for (int h = 0; h < kHidden; ++h) {
            float s = b1[(size_t) h];
            for (int i = 0; i < kBands + kEmbed; ++i) s += w1[(size_t) h][(size_t) i] * in[(size_t) i];
            hid[(size_t) h] = std::tanh(s);
        }

        // Output layer: 24 Bark dB adjustments + HNR bias
        const float activeMix = p.characterIndex > 0
                                    ? 1.0f
                                    : std::clamp(p.regen + std::abs(p.formantSemitones) / 4.0f + std::abs(p.pitchSemitones) / 8.0f +
                                                     std::abs(p.glottal) + p.growl,
                                                 0.0f, 1.0f);
        std::array<float, kBands> barkDb{};
        for (int b = 0; b < kBands; ++b) {
            float s = b2[(size_t) b];
            for (int h = 0; h < kHidden; ++h) s += w2[(size_t) b][(size_t) h] * hid[(size_t) h];
            barkDb[(size_t) b] = std::clamp(s * activeMix, -8.5f, 8.5f);
        }
        float hnr = 0.0f;
        for (int h = 0; h < kHidden; ++h) hnr += wHnr[(size_t) h] * hid[(size_t) h];
        outHnrBias = std::clamp(hnr * activeMix, -0.35f, 0.35f);

        // Smooth Bark-to-bin interpolation
        int bIdx = 0;
        for (int k = 0; k < halfBins; ++k) {
            const float fk = (float) k;
            while (bIdx + 1 < kBands - 1 && bandCentreBin[(size_t) (bIdx + 1)] < fk) ++bIdx;
            const float c0 = bandCentreBin[(size_t) bIdx];
            const float c1 = bandCentreBin[(size_t) std::min(kBands - 1, bIdx + 1)];
            const float t = c1 > c0 + 1e-4f ? std::clamp((fk - c0) / (c1 - c0), 0.0f, 1.0f) : 0.0f;
            const float db = barkDb[(size_t) bIdx] + (barkDb[(size_t) std::min(kBands - 1, bIdx + 1)] - barkDb[(size_t) bIdx]) * t;
            outGain[k] = std::pow(10.0f, db / 20.0f);
        }
    }

private:
    static std::array<float, kEmbed> buildEmbedding(const HybridParams& p) {
        // Blend character preset embedding with live knob state for seamless manual tweaking
        static constexpr std::array<std::array<float, kEmbed>, 13> archetypes = {{
            // size, chestHead, warmth, nasal, growl, breath, clarity, youth
            { 0.00f,  0.00f,  0.00f, 0.00f, 0.00f, 0.00f, 0.00f,  0.00f}, // natural
            {-0.55f, -0.55f,  0.20f, 0.00f, 0.18f, 0.06f, 0.50f, -0.05f}, // bigman
            {-0.85f, -0.80f,  0.35f, 0.00f, 0.38f, 0.10f, 0.55f, -0.12f}, // giant
            {-0.95f, -0.90f,  0.25f, 0.00f, 0.68f, 0.22f, 0.45f, -0.18f}, // monster
            {-0.18f, -0.30f,  0.10f, 0.00f, 0.06f, 0.05f, 0.40f,  0.00f}, // man
            { 0.55f,  0.55f, -0.10f, 0.00f, 0.00f, 0.18f, 0.45f,  0.10f}, // woman
            { 0.90f,  0.75f, -0.30f, 0.03f, 0.00f, 0.22f, 0.40f,  0.80f}, // girl
            { 0.72f,  0.45f, -0.22f, 0.00f, 0.00f, 0.14f, 0.35f,  0.70f}, // boy
            {-0.10f, -0.25f,  0.45f, 0.05f, 0.28f, 0.24f, 0.25f, -0.45f}, // oldman
            { 0.38f,  0.30f,  0.40f, 0.07f, 0.18f, 0.28f, 0.25f, -0.45f}, // oldwoman
            { 0.48f, -0.45f,  0.15f, 0.30f, 0.42f, 0.25f, 0.60f, -0.35f}, // witch
            { 0.25f, -0.65f,  0.18f, 0.10f, 0.30f, 0.08f, 0.50f, -0.10f}, // dwarf
            { 0.65f, -0.50f, -0.20f, 0.28f, 0.48f, 0.18f, 0.65f,  0.10f}, // goblin
        }};
        const int idx = std::clamp(p.characterIndex, 0, 12);
        std::array<float, kEmbed> e = archetypes[(size_t) idx];
        if (idx == 0) {
            e[0] = std::clamp(p.formantSemitones / 5.5f, -1.0f, 1.0f);
            e[6] = std::min(0.45f, std::abs(p.pitchSemitones) * 0.04f + std::abs(p.formantSemitones) * 0.05f);
        }
        e[1] = std::clamp(p.glottal, -1.0f, 1.0f);
        e[3] = std::clamp(p.nasal, 0.0f, 1.0f);
        e[4] = std::clamp(p.growl, 0.0f, 1.0f);
        e[5] = std::clamp(p.breath, 0.0f, 1.0f);
        e[7] = std::clamp(-p.age / 50.0f, -1.0f, 1.0f);
        return e;
    }

    void buildWeights() {
        // Reference neutral speech Bark curve (normalized zero-mean log profile)
        neutralRefBark = {0.2f, 0.5f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f, -0.1f, -0.1f,
                          -0.2f, -0.2f, -0.2f, -0.3f, -0.3f, -0.4f, -0.4f, -0.5f, -0.5f, -0.6f, -0.7f, -0.8f};
        for (auto& r : w1) r.fill(0.0f);
        for (auto& r : w2) r.fill(0.0f);
        b1.fill(0.0f);
        b2.fill(0.0f);
        wHnr.fill(0.0f);

        // Neurons 0..7: Character archetype -> Bark resonance curves
        // h0: Deep chest authority (size < 0, chestHead < 0) -> boosts 70..250 Hz, scoops 380..550 Hz boxiness
        w1[0][kBands + 0] = -1.1f;
        w1[0][kBands + 1] = -0.8f;
        for (int b = 0; b <= 2; ++b) w2[(size_t) b][0] = 3.2f;
        for (int b = 4; b <= 6; ++b) w2[(size_t) b][0] = -2.2f; // anti-mud clarity scoop

        // h1: Light female/child vocal clarity (size > 0, chestHead > 0) -> cleans <180 Hz rumble, lifts 1.5..4 kHz
        w1[1][kBands + 0] = 1.0f;
        w1[1][kBands + 1] = 0.9f;
        w2[0][1] = -4.5f;
        w2[1][1] = -3.2f;
        w2[2][1] = -1.4f;
        for (int b = 10; b <= 16; ++b) w2[(size_t) b][1] = 2.0f;

        // h2: Dubbing/Cinematic presence (clarity) -> lifts 2.4..3.8 kHz intelligibility
        w1[2][kBands + 6] = 1.4f;
        for (int b = 13; b <= 16; ++b) w2[(size_t) b][2] = 2.6f;

        // h3: Warm/Aged damping (warmth > 0) -> softens upper glare >4.5 kHz, warms 400..900 Hz
        w1[3][kBands + 2] = 1.2f;
        for (int b = 4; b <= 7; ++b) w2[(size_t) b][3] = 1.4f;
        for (int b = 18; b <= 23; ++b) w2[(size_t) b][3] = -2.4f;

        // h4: Silky airy tail (breath + youth) -> gentle 7..13 kHz air
        w1[4][kBands + 5] = 1.1f;
        w1[4][kBands + 7] = 0.5f;
        for (int b = 20; b <= 23; ++b) w2[(size_t) b][4] = 2.8f;

        // h5: Nasal/witch/goblin pharyngeal bite
        w1[5][kBands + 3] = 1.8f;
        w2[9][5] = 3.2f;
        w2[10][5] = 3.8f;
        w2[11][5] = 2.5f;
        w2[3][5] = -2.0f;

        // h6: Monster/Giant sub-growl resonance (growl)
        w1[6][kBands + 4] = 1.5f;
        w2[0][6] = 2.8f;
        w2[1][6] = 2.2f;
        w2[12][6] = 1.5f;
        wHnr[6] = -0.22f;

        // h7: Head voice silky HNR boost
        w1[7][kBands + 1] = 1.1f;
        w1[7][kBands + 5] = 0.7f;
        wHnr[7] = 0.16f;

        // Neurons 8..31: Non-linear per-band dynamic resonance regulators (tame spiky excesses & fill deficits)
        for (int b = 0; b < kBands; ++b) {
            const int h = 8 + b;
            // Detect excess energy in band b (normalized speaker-compensated Bark input)
            w1[(size_t) h][(size_t) b] = 1.6f;
            // Modulate sensitivity with character clarity/smoothness
            w1[(size_t) h][kBands + 0] = (b < 3) ? -0.35f : (b > 14 && b < 19) ? 0.25f : 0.0f;
            // Soft negative feedback compresses unnatural spectral spikes while gently lifting starved bands
            w2[(size_t) b][(size_t) h] = -1.6f;
            if (b > 0) w2[(size_t) (b - 1)][(size_t) h] = -0.35f;
            if (b + 1 < kBands) w2[(size_t) (b + 1)][(size_t) h] = -0.35f;
        }
    }

    double sr = 48000.0;
    int halfBins = 1025;
    int speakerFrames = 0;
    std::array<int, kBands> bandLo{}, bandHi{};
    std::array<float, kBands> bandCentreBin{}, speakerBark{}, neutralRefBark{};
    std::array<std::array<float, kBands + kEmbed>, kHidden> w1{};
    std::array<float, kHidden> b1{}, wHnr{};
    std::array<std::array<float, kHidden>, kBands> w2{};
    std::array<float, kBands> b2{};
};

/**
 * Sample-accurate hybrid voice regeneration engine:
 *  - Aligns dry audio, spectral pitch-shifted audio, and pitch tracker timelines to a single clock
 *  - Performs true cepstral source-filter separation and non-linear F1/F2/F3/F4 vocal tract warping
 *  - Synthesizes fresh band-limited LF glottal harmonics, subharmonics, and pitch-synchronous breath
 *  - Preserves unvoiced sibilants and plosive consonants unshifted via multi-band Consonant Guard
 */
class HybridRegenerator {
public:
    void prepare(double sampleRate, int numChannels, int maxBlock, int spectralLatencySamples, int64_t neuralDelaySamples,
                 int neuralSpacingSamples) {
        sr = std::max(8000.0, sampleRate);
        ch = std::max(1, numChannels);
        warper.prepare(sr);
        lfTable.prepare();

        fftSize = warper.fftLength();
        halfBins = warper.numBins();
        hop = std::max(64, fftSize / 8); // 8x overlap for artifact-free WOLA
        binHz = warper.hzPerBin();

        neuralLatSamples = neuralDelaySamples + neuralSpacingSamples + 128;
        coreAlignLat = std::max((int) neuralLatSamples, spectralLatencySamples);
        spectralPad = std::max(0, coreAlignLat - spectralLatencySamples);
        totalLatency = coreAlignLat + fftSize;

        // Precompute Hann windows and FFT twiddles
        hannWin.resize((size_t) fftSize);
        synthWin.resize((size_t) fftSize);
        // 8x overlap Hann*Hann sum = 3.0 -> divide synthesis window by 3.0 for exact unity OLA
        const float olaNorm = 1.0f / 3.0f;
        for (int i = 0; i < fftSize; ++i) {
            const float w = 0.5f - 0.5f * std::cos(2.0f * (float) M_PI * i / fftSize);
            hannWin[(size_t) i] = w;
            synthWin[(size_t) i] = w * olaNorm;
        }
        twFwd.resize((size_t) fftSize / 2);
        twInv.resize((size_t) fftSize / 2);
        for (int k = 0; k < fftSize / 2; ++k) {
            const float ang = (float) (2.0 * M_PI * k / fftSize);
            twFwd[(size_t) k] = {std::cos(ang), -std::sin(ang)};
            twInv[(size_t) k] = {std::cos(ang), std::sin(ang)};
        }

        // Delay buffers (power-of-2 ring buffers)
        int dSize = 1;
        while (dSize < coreAlignLat + maxBlock + fftSize * 2) dSize <<= 1;
        delayMask = dSize - 1;
        dryAlignRing.assign((size_t) ch, std::vector<float>((size_t) dSize, 0.0f));
        specAlignRing.assign((size_t) ch, std::vector<float>((size_t) dSize, 0.0f));

        // STFT input/output rings per channel
        stftMask = fftSize * 2 - 1; // 2*N power-of-2
        dryInRing.assign((size_t) ch, std::vector<float>((size_t) (fftSize * 2), 0.0f));
        specInRing.assign((size_t) ch, std::vector<float>((size_t) (fftSize * 2), 0.0f));
        outRing.assign((size_t) ch, std::vector<float>((size_t) (fftSize * 2), 0.0f));
        lfInRing.assign((size_t) (fftSize * 2), 0.0f);
        breathInRing.assign((size_t) (fftSize * 2), 0.0f);

        // Spectral scratch buffers (allocated here, zero audio-thread allocation)
        monoDryFrame.assign((size_t) fftSize, 0.0f);
        fftWork.assign((size_t) fftSize, {});
        lfWork.assign((size_t) fftSize, {});
        breathWork.assign((size_t) fftSize, {});
        specChanWork.assign((size_t) fftSize, {});
        dryChanWork.assign((size_t) fftSize, {});
        outChanWork.assign((size_t) fftSize, {});

        magDryMono.assign((size_t) halfBins, 0.0f);
        prevMagDryMono.assign((size_t) halfBins, 0.0f);
        magSpecChan.assign((size_t) halfBins, 0.0f);
        envDryFull.assign((size_t) halfBins, 0.0f);
        pureTractDry.assign((size_t) halfBins, 0.0f);
        tiltDry.assign((size_t) halfBins, 0.0f);
        warpedTract.assign((size_t) halfBins, 0.0f);
        neuralGain.assign((size_t) halfBins, 1.0f);
        targetTract.assign((size_t) halfBins, 1.0f);
        targetTractSpec.assign((size_t) halfBins, 1.0f);
        consGain.assign((size_t) halfBins, 1.0f);
        envSpec.assign((size_t) halfBins, 1.0f);
        lfTiltComp.assign((size_t) halfBins, 1.0f);

        // Precompute neutral LF-tilt compensation curve (~ +0.8 dB/oct gentle upper balance)
        for (int k = 0; k < halfBins; ++k) {
            const float f = std::max(80.0f, (float) k * binHz);
            // Keeps regenerated LF source crisp without excessive dark roll-off
            lfTiltComp[(size_t) k] = std::clamp(std::pow(f / 500.0f, 0.25f), 0.65f, 1.85f);
        }

        timbre.prepare(sr, fftSize);
        reset();
    }

    void reset() {
        for (auto& v : dryAlignRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : specAlignRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : dryInRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : specInRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : outRing) std::fill(v.begin(), v.end(), 0.0f);
        std::fill(lfInRing.begin(), lfInRing.end(), 0.0f);
        std::fill(breathInRing.begin(), breathInRing.end(), 0.0f);
        std::fill(prevMagDryMono.begin(), prevMagDryMono.end(), 0.0f);

        neuralTimeline.reset();
        yinTimeline.reset();
        sampleClock = 0;
        hopCounter = 0;
        lfPhase = 0.0;
        unwrappedPeriods = 0;
        sub2Phase = 0.0;
        sub3Phase = 0.0;
        tremorPhase = 0.0;
        rngState = 0x9E3779B9u;
        jitterSmooth = 0.0f;
        breathHpPrevIn = breathHpPrevOut = 0.0f;
        pllRe = pllIm = 0.0f;
        smoothVoiced = 0.0f;
        smoothConsonant = 0.0f;
        smoothTargetF0 = 140.0f;
        smoothLfGain = 1.0f;
        prevAlignedDry = 0.0f;
        status = HybridStatus{};
    }

    void resetVoiceProfile() { timbre.resetSpeaker(); }

    int latencySamples() const { return totalLatency; }

    void pushNeuralEstimate(const PitchEstimate& e) {
        const float f0 = e.candidate > 0.0f ? (float) (sr / e.candidate) : (e.period > 0.0f ? (float) (sr / e.period) : 0.0f);
        neuralTimeline.push(e.time, f0, e.confidence, e.aperiodicity, e.rms);
    }

    void pushYinEstimate(const PitchEstimate& e) {
        const float f0 = e.period > 0.0f ? (float) (sr / e.period) : 0.0f;
        const float conf = e.voiced() ? std::clamp(1.0f - e.aperiodicity / 0.40f, 0.0f, 1.0f) : 0.0f;
        yinTimeline.push(e.time, f0, conf, e.aperiodicity, e.rms);
    }

    HybridStatus getStatus() const { return status; }

    /**
     * Processes `n` samples:
     *  - `rawInput[c][0..n-1]`: original unshifted dry block
     *  - `specAudio[c][0..n-1]`: Signalsmith pitch-shifted audio (in-place overwritten with hybrid output)
     */
    void process(const float* const* rawInput, float* const* specAudio, int n, const HybridParams& p, bool useNeural) {
        for (int i = 0; i < n; ++i) {
            const int wIdx = (int) (sampleClock & delayMask);
            const int dryRIdx = (int) ((sampleClock - coreAlignLat) & delayMask);
            const int specRIdx = (int) ((sampleClock - spectralPad) & delayMask);
            const int sIdx = (int) (sampleClock & stftMask);

            float alignedDryMono = 0.0f;
            float alignedSpecMono = 0.0f;
            for (int c = 0; c < ch; ++c) {
                dryAlignRing[(size_t) c][(size_t) wIdx] = rawInput[c][i];
                specAlignRing[(size_t) c][(size_t) wIdx] = specAudio[c][i];

                const float dSamp = sampleClock >= coreAlignLat ? dryAlignRing[(size_t) c][(size_t) dryRIdx] : 0.0f;
                const float sSamp = sampleClock >= spectralPad ? specAlignRing[(size_t) c][(size_t) specRIdx] : 0.0f;

                dryInRing[(size_t) c][(size_t) sIdx] = dSamp;
                specInRing[(size_t) c][(size_t) sIdx] = sSamp;
                alignedDryMono += dSamp;
                alignedSpecMono += sSamp;

                // Read previously overlap-added STFT output and clear cell for future OLA
                specAudio[c][i] = outRing[(size_t) c][(size_t) sIdx];
                outRing[(size_t) c][(size_t) sIdx] = 0.0f;
            }
            alignedDryMono /= (float) ch;
            alignedSpecMono /= (float) ch;

            // Advance sample-continuous LF glottal source + subharmonics + pitch-sync breath
            synthesizeExcitationSample(sIdx, alignedSpecMono, p);

            ++sampleClock;
            if (++hopCounter >= hop) {
                hopCounter = 0;
                processFrame(p, useNeural);
            }
        }
    }

private:
    float nextNoise() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return ((float) (rngState & 0x00FFFFFFu) / 8388608.0f) - 1.0f;
    }

    void synthesizeExcitationSample(int sIdx, float alignedSpecMono, const HybridParams& p) {
        // Gentle age tremor (~5.5 Hz) and organic jitter
        const float agePos = std::clamp(p.age / 50.0f, 0.0f, 1.0f);
        tremorPhase += 5.4 / sr;
        if (tremorPhase >= 1.0) tremorPhase -= 1.0;

        jitterSmooth += (nextNoise() - jitterSmooth) * (1.0f - (float) std::exp(-1.0 / (0.004 * sr)));
        const float ageModSemitones = agePos > 0.02f ? agePos * agePos * (0.28f * (float) std::sin(2.0 * M_PI * tremorPhase) + 0.14f * jitterSmooth) : 0.0f;

        float targetF0 = smoothTargetF0 * std::pow(2.0f, ageModSemitones / 12.0f);
        targetF0 = std::clamp(targetF0, 42.0f, 1150.0f);

        // Soft complex demodulator at targetF0 to gently keep LF phase in lock with Signalsmith fundamental
        if (smoothVoiced > 0.45f && p.regen > 0.02f && p.regen < 0.98f) {
            const float ang = (float) (2.0 * M_PI * lfPhase);
            const float c = std::cos(ang), s = std::sin(ang);
            const float kPll = 1.0f - (float) std::exp(-1.0 / (0.025 * sr));
            pllRe += (alignedSpecMono * c - pllRe) * kPll;
            pllIm += (-alignedSpecMono * s - pllIm) * kPll;
            const float mag2 = pllRe * pllRe + pllIm * pllIm;
            if (mag2 > 1e-8f) {
                const float err = std::atan2(-pllIm, pllRe);
                lfPhase += 0.0012 * std::sin(err);
            }
        }

        const double inc = (double) targetF0 / sr;
        lfPhase += inc;
        if (lfPhase >= 1.0) {
            lfPhase -= std::floor(lfPhase);
            ++unwrappedPeriods;
        }
        if (lfPhase < 0.0) lfPhase += 1.0;

        sub2Phase += inc * 0.5;
        if (sub2Phase >= 1.0) sub2Phase -= std::floor(sub2Phase);
        sub3Phase += inc * (1.0 / 3.0);
        if (sub3Phase >= 1.0) sub3Phase -= std::floor(sub3Phase);

        const float f0Norm = (float) inc;
        // Age slightly increases open quotient (softer fold closure)
        const float glottalEff = std::clamp(p.glottal + agePos * 0.15f, -1.0f, 1.0f);
        float glot = lfTable.sample((float) lfPhase, glottalEff, f0Norm);

        // Growl / Vocal fry: diplophonia (period doubling) + F0/2 and F0/3 subharmonics
        const float effortBoost = std::clamp(0.6f + 0.55f * p.effort, 0.4f, 1.4f);
        const float growlAmt = std::clamp(p.growl * effortBoost, 0.0f, 1.0f);
        if (growlAmt > 0.01f) {
            const float periodGain = (unwrappedPeriods & 1) ? (1.0f - 0.35f * growlAmt) : 1.0f;
            const float s2 = lfTable.sample((float) sub2Phase, -0.85f, f0Norm * 0.5f);
            const float s3 = lfTable.sample((float) sub3Phase, -0.90f, f0Norm * 0.333f);
            glot = glot * periodGain + growlAmt * (0.48f * s2 + 0.24f * s3);
        }

        // Gentle age amplitude shimmer
        if (agePos > 0.02f) glot *= 1.0f + 0.10f * agePos * jitterSmooth;
        lfInRing[(size_t) sIdx] = glot * smoothVoiced;

        // Pitch-synchronous glottal breath: 700 Hz high-passed noise modulated by glottal open phase
        const float rawNoise = nextNoise();
        const float hpAlpha = (float) std::exp(-2.0 * M_PI * 700.0 / sr);
        const float hp = hpAlpha * (breathHpPrevOut + rawNoise - breathHpPrevIn);
        breathHpPrevIn = rawNoise;
        breathHpPrevOut = hp;
        const float oq = 0.32f + 0.50f * (glottalEff + 1.0f) * 0.5f;
        const float openEnv = lfPhase < oq ? std::sin((float) M_PI * (float) lfPhase / oq) : 0.0f;
        breathInRing[(size_t) sIdx] = hp * (0.35f + 0.65f * openEnv * openEnv);
    }

    void processFrame(const HybridParams& p, bool useNeural) {
        const int64_t frameEnd = sampleClock;
        const int64_t frameCentreInInput = frameEnd - coreAlignLat - fftSize / 2;

        // 1. Lookup aligned pitch estimate at the exact frame centre timestamp
        const auto ne = neuralTimeline.query(frameCentreInInput);
        const auto ye = yinTimeline.query(frameCentreInInput);
        const float f0In = (useNeural && ne.f0 > 42.0f) ? ne.f0 : (ye.f0 > 42.0f ? ye.f0 : ne.f0);
        const float trackerConf = useNeural ? ne.confidence : ye.confidence;
        const float aperiodicity = std::min(ne.aperiodicity, ye.aperiodicity);

        // 2. Gather delayed mono dry frame and compute dry magnitude + acoustic detectors
        double drySq = 0.0;
        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((frameEnd - fftSize + m) & stftMask);
            float s = 0.0f;
            for (int c = 0; c < ch; ++c) s += dryInRing[(size_t) c][(size_t) rIdx];
            s /= (float) ch;
            monoDryFrame[(size_t) m] = s;
            drySq += (double) s * s;
            fftWork[(size_t) m] = {s * hannWin[(size_t) m], 0.0f};
        }
        const float frameRms = (float) std::sqrt(drySq / fftSize);
        fft(fftWork, false);

        double logGeoSum = 0.0, arithSum = 0.0;
        int flatCnt = 0;
        double lowE = 0.0, highE = 0.0, fluxPos = 0.0, totalMag = 0.0;
        const int bin500 = std::max(1, (int) (500.0f / binHz));
        const int bin1500 = std::max(bin500 + 1, (int) (1500.0f / binHz));
        const int bin3200 = std::max(bin1500 + 1, (int) (3200.0f / binHz));
        const int bin11k = std::min(halfBins - 1, (int) (11000.0f / binHz));

        for (int k = 0; k < halfBins; ++k) {
            const float m = std::abs(fftWork[(size_t) k]);
            magDryMono[(size_t) k] = m;
            if (k >= 2 && k <= bin11k) {
                const float lm = std::log(std::max(1e-6f, m));
                const float prevLm = std::log(std::max(1e-6f, prevMagDryMono[(size_t) k]));
                if (lm > prevLm) fluxPos += (lm - prevLm);
                totalMag += m;
                if (k >= bin500) {
                    const float pwr = m * m + 1e-10f;
                    logGeoSum += std::log(pwr);
                    arithSum += pwr;
                    ++flatCnt;
                }
                if (k < bin1500) lowE += (double) m * m;
                if (k >= bin3200) highE += (double) m * m;
            }
            prevMagDryMono[(size_t) k] = m;
        }

        const float spectralFlatness =
            flatCnt > 0 && arithSum > 1e-12 ? std::clamp((float) (std::exp(logGeoSum / flatCnt) / (arithSum / flatCnt)), 0.0f, 1.0f) : 0.0f;
        const float hfRatio = (float) (highE / (lowE + 1e-9));
        const float sibilantScore = std::clamp(0.55f * spectralFlatness + 0.45f * (hfRatio / (hfRatio + 0.45f)), 0.0f, 1.0f);
        const float transientScore = frameRms > 0.002f ? std::clamp((float) (fluxPos / std::max(40.0, (double) bin11k * 0.45)), 0.0f, 1.0f) : 0.0f;

        // 3. Combined continuous voiced mask and consonant guard mask
        const float rawTrackerVoiced =
            frameRms < 0.0012f ? 0.0f : std::clamp((trackerConf - 0.30f) / 0.35f, 0.0f, 1.0f) * std::clamp(1.2f - 0.7f * aperiodicity, 0.0f, 1.0f);
        const float targetVoiced = rawTrackerVoiced * (1.0f - 0.80f * std::max(0.0f, sibilantScore - 0.35f)) * (1.0f - 0.55f * transientScore);
        const float attackCoef = targetVoiced > smoothVoiced ? 0.45f : 0.18f;
        smoothVoiced += (targetVoiced - smoothVoiced) * attackCoef;
        const float consActivity = p.consonantGuard ? std::clamp(std::max(1.0f - smoothVoiced, std::max(sibilantScore, transientScore)), 0.0f, 1.0f) : 0.0f;
        smoothConsonant += (consActivity - smoothConsonant) * 0.35f;

        // 4. Update smooth target F0 for the LF glottal oscillator
        const float pitchRatio = std::pow(2.0f, p.pitchSemitones / 12.0f);
        if (f0In > 42.0f && rawTrackerVoiced > 0.25f) {
            float desiredF0 = std::clamp(f0In * pitchRatio, 45.0f, 1100.0f);
            // When steady pitch is active, compress fast fluctuations slightly toward learned character center
            if (p.steadyPitch && p.speakerMedianF0 > 40.0f) {
                const float targetCentre = std::clamp(p.speakerMedianF0 * pitchRatio, 45.0f, 1100.0f);
                desiredF0 = std::exp(std::log(desiredF0) * 0.72f + std::log(targetCentre) * 0.28f);
            }
            smoothTargetF0 += (desiredF0 - smoothTargetF0) * 0.45f;
        }

        // 5. Cepstral source-filter extraction + non-linear F1/F2/F3/F4 warp + Neural Bark timbre
        warper.extractEnvelope(magDryMono.data(), f0In, envDryFull.data(), pureTractDry.data(), tiltDry.data());
        timbre.learnSpeaker(envDryFull.data(), smoothVoiced);

        const float activeMix = p.characterIndex > 0
                                    ? 1.0f
                                    : std::clamp(p.regen + std::abs(p.formantSemitones) / 4.0f + std::abs(p.pitchSemitones) / 8.0f +
                                                     std::abs(p.glottal) + p.growl,
                                                 0.0f, 1.0f);
        const float sharpness = activeMix * std::clamp(0.35f + 0.35f * p.regen + 0.25f * std::abs(p.formantSemitones) / 8.0f, 0.2f, 0.9f);
        warper.warpEnvelope(pureTractDry.data(), p.formantSemitones, sharpness, warpedTract.data());

        float hnrBias = 0.0f;
        timbre.evaluate(warpedTract.data(), p, neuralGain.data(), hnrBias);

        // Build target formant tract filter and gentle consonant mouth-size filter
        const float consFormant = std::clamp(p.formantSemitones * 0.30f, -2.5f, 2.5f);
        warper.warpEnvelope(pureTractDry.data(), consFormant, 0.15f * activeMix, consGain.data());
        for (int k = 0; k < halfBins; ++k) {
            const float specT = std::clamp(warpedTract[(size_t) k] * neuralGain[(size_t) k], 0.04f, 14.0f);
            targetTractSpec[(size_t) k] = specT;
            targetTract[(size_t) k] = std::clamp(specT * lfTiltComp[(size_t) k], 0.04f, 14.0f);
            consGain[(size_t) k] = std::clamp(consGain[(size_t) k] / std::max(1e-4f, pureTractDry[(size_t) k]), 0.35f, 2.8f);
        }

        // 6. FFT of the continuous LF glottal source and pitch-sync breath excitation
        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((frameEnd - fftSize + m) & stftMask);
            lfWork[(size_t) m] = {lfInRing[(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
            breathWork[(size_t) m] = {breathInRing[(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
        }
        fft(lfWork, false);
        fft(breathWork, false);

        const float regenAmt = std::clamp(p.regen * smoothVoiced, 0.0f, 1.0f);
        const float breathMix = std::clamp(p.breath + std::max(0.0f, hnrBias) * 0.15f, 0.0f, 1.0f) * smoothVoiced;
        const float consProtect = p.consonantGuard ? std::clamp(std::max(1.0f - smoothVoiced, transientScore * 0.85f), 0.0f, 1.0f) : 0.0f;
        const float hfSibilant = p.consonantGuard ? std::clamp((sibilantScore - 0.25f) / 0.35f, 0.0f, 1.0f) : 0.0f;
        const int bin2400 = (int) (2400.0f / binHz);
        const int bin3600 = std::max(bin2400 + 1, (int) (3600.0f / binHz));
        const int bin4500 = std::min(halfBins - 1, (int) (4500.0f / binHz));

        // 7. Per-channel spectral fusion & inverse FFT overlap-add
        for (int c = 0; c < ch; ++c) {
            for (int m = 0; m < fftSize; ++m) {
                const int rIdx = (int) ((frameEnd - fftSize + m) & stftMask);
                specChanWork[(size_t) m] = {specInRing[(size_t) c][(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
                dryChanWork[(size_t) m] = {dryInRing[(size_t) c][(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
            }
            fft(specChanWork, false);
            fft(dryChanWork, false);

            for (int k = 0; k < halfBins; ++k) magSpecChan[(size_t) k] = std::abs(specChanWork[(size_t) k]);
            warper.extractEnvelope(magSpecChan.data(), smoothTargetF0, envSpec.data(), nullptr, nullptr);

            // Compute envelope-corrected Signalsmith power & raw LF power in voiced band for exact level matching
            double specPow = 0.0, lfPow = 0.0, breathPow = 0.0;
            for (int k = 1; k <= bin4500; ++k) {
                const float envScale = std::clamp(targetTractSpec[(size_t) k] * tiltDry[(size_t) k] / std::max(1e-5f, envSpec[(size_t) k]), 0.08f, 6.0f);
                const float ms = magSpecChan[(size_t) k] * envScale;
                const float ml = std::abs(lfWork[(size_t) k]) * targetTract[(size_t) k];
                const float mb = std::abs(breathWork[(size_t) k]) * targetTract[(size_t) k];
                specPow += (double) ms * ms;
                lfPow += (double) ml * ml;
                breathPow += (double) mb * mb;
            }
            const float targetLfGain = lfPow > 1e-10 ? std::clamp((float) std::sqrt(specPow / lfPow), 0.0f, 8.0f) : 0.0f;
            if (c == 0) smoothLfGain += (targetLfGain - smoothLfGain) * 0.40f;
            const float lfGain = c == 0 ? smoothLfGain : targetLfGain;
            const float breathGain = breathPow > 1e-10 ? std::clamp((float) std::sqrt(specPow / breathPow), 0.0f, 4.0f) * breathMix * 0.42f : 0.0f;

            double dryConsPow = 0.0, warpedConsPow = 0.0;
            for (int k = bin2400; k < halfBins; ++k) {
                const float dm = std::abs(dryChanWork[(size_t) k]);
                const float cm = dm * consGain[(size_t) k];
                dryConsPow += (double) dm * dm;
                warpedConsPow += (double) cm * cm;
            }
            const float consScale = warpedConsPow > 1e-10 ? std::clamp((float) std::sqrt(dryConsPow / warpedConsPow), 0.5f, 2.0f) : 1.0f;

            for (int k = 0; k < halfBins; ++k) {
                const float envScale = std::clamp(targetTractSpec[(size_t) k] * tiltDry[(size_t) k] / std::max(1e-5f, envSpec[(size_t) k]), 0.08f, 6.0f);
                const std::complex<float> ySpec = specChanWork[(size_t) k] * envScale;
                const std::complex<float> yLf = lfWork[(size_t) k] * (targetTract[(size_t) k] * lfGain);
                const std::complex<float> yBreath = breathWork[(size_t) k] * (targetTract[(size_t) k] * breathGain);

                // Comb-free hybrid crossfade with power-preserving magnitude correction
                std::complex<float> yVoiced = ySpec * (1.0f - regenAmt) + yLf * regenAmt;
                const float magNow = std::abs(yVoiced);
                const float magGoal = std::abs(ySpec) * (1.0f - regenAmt) + std::abs(yLf) * regenAmt;
                if (magNow > 1e-6f && regenAmt > 0.02f && regenAmt < 0.98f) {
                    const float fix = std::clamp(magGoal / magNow, 0.75f, 1.75f);
                    yVoiced *= fix;
                }
                yVoiced += yBreath;

                // Consonant Guard: unshifted sibilants & plosives with subtle mouth-size F3/F4 tilt
                float hf = 0.0f;
                if (k >= bin3600) hf = 1.0f;
                else if (k > bin2400) {
                    const float u = (float) (k - bin2400) / (float) (bin3600 - bin2400);
                    hf = 0.5f - 0.5f * std::cos((float) M_PI * u);
                }
                const float unshiftedMix = std::clamp(consProtect + hf * hfSibilant * (1.0f - consProtect), 0.0f, 1.0f);
                const std::complex<float> yCons = dryChanWork[(size_t) k] * (consGain[(size_t) k] * consScale);
                const std::complex<float> yOut = yVoiced * (1.0f - unshiftedMix) + yCons * unshiftedMix;

                outChanWork[(size_t) k] = yOut;
                if (k > 0 && k < fftSize / 2) outChanWork[(size_t) (fftSize - k)] = std::conj(yOut);
            }

            fft(outChanWork, true);

            // Overlap-add into output ring right after frameEnd (total delay = coreAlignLat + fftSize)
            for (int m = 0; m < fftSize; ++m) {
                const int wIdx = (int) ((frameEnd + m) & stftMask);
                float v = outChanWork[(size_t) m].real() * synthWin[(size_t) m];
                if (!std::isfinite(v)) v = 0.0f;
                outRing[(size_t) c][(size_t) wIdx] += v;
            }
        }

        status.voiced = smoothVoiced;
        status.consonant = smoothConsonant;
        status.regen = regenAmt;
        status.subharmonic = std::clamp(p.growl * smoothVoiced, 0.0f, 1.0f);
    }

    void fft(std::vector<std::complex<float>>& a, bool inverse) const {
        const int n = fftSize;
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[(size_t) i], a[(size_t) j]);
        }
        const auto& tw = inverse ? twInv : twFwd;
        for (int len = 2; len <= n; len <<= 1) {
            const int step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int j = 0; j < len / 2; ++j) {
                    const auto u = a[(size_t) (i + j)];
                    const auto v = a[(size_t) (i + j + len / 2)] * tw[(size_t) (j * step)];
                    a[(size_t) (i + j)] = u + v;
                    a[(size_t) (i + j + len / 2)] = u - v;
                }
            }
        }
        if (inverse) {
            const float invN = 1.0f / (float) n;
            for (auto& x : a) x *= invN;
        }
    }

    double sr = 48000.0;
    int ch = 2, fftSize = 2048, halfBins = 1025, hop = 256;
    float binHz = 23.4375f;
    int64_t neuralLatSamples = 8800;
    int coreAlignLat = 8800, spectralPad = 0, totalLatency = 10848;
    int delayMask = 0, stftMask = 0;

    VocalTractWarper warper;
    LFGlottalTable lfTable;
    NeuralTimbreShaper timbre;
    PitchTimeline neuralTimeline, yinTimeline;

    std::vector<float> hannWin, synthWin;
    std::vector<std::complex<float>> twFwd, twInv;
    std::vector<std::vector<float>> dryAlignRing, specAlignRing, dryInRing, specInRing, outRing;
    std::vector<float> lfInRing, breathInRing;

    std::vector<float> monoDryFrame, magDryMono, prevMagDryMono, magSpecChan, envDryFull, pureTractDry, tiltDry;
    std::vector<float> warpedTract, neuralGain, targetTract, targetTractSpec, consGain, envSpec, lfTiltComp;
    std::vector<std::complex<float>> fftWork, lfWork, breathWork, specChanWork, dryChanWork, outChanWork;

    int64_t sampleClock = 0;
    int hopCounter = 0;
    double lfPhase = 0.0, sub2Phase = 0.0, sub3Phase = 0.0, tremorPhase = 0.0;
    uint64_t unwrappedPeriods = 0;
    uint32_t rngState = 0x9E3779B9u;
    float jitterSmooth = 0.0f, breathHpPrevIn = 0.0f, breathHpPrevOut = 0.0f;
    float pllRe = 0.0f, pllIm = 0.0f;
    float smoothVoiced = 0.0f, smoothConsonant = 0.0f, smoothTargetF0 = 140.0f, smoothLfGain = 1.0f;
    float prevAlignedDry = 0.0f;
    HybridStatus status{};
};

} // namespace adub
