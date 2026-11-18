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
 * Sample-aligned, phase-locked hybrid voice regeneration engine:
 *  - Low-latency alignment with Signalsmith Stretch (zero dry voice bleed below 3.5 kHz)
 *  - Phase-locked analytic SVF fundamental follower driving:
 *      * band-limited LF glottal harmonics (Zero beating, zero phase cancellation)
 *      * zero-crossing continuous F0/2 & F0/3 subharmonics
 *      * upper-harmonic asymmetric exciter (> 2.2 kHz)
 *      * syllable-gated pitch-synchronous velvet breath
 *  - Causal minimum-phase vocal tract warping + 24-Bark neural timbre shaper
 *  - Strictly unvoiced >3.5 kHz Consonant Guard (s, ş, ç, t, k, f)
 */
class HybridRegenerator {
public:
    void prepare(double sampleRate, int numChannels, int maxBlock, int spectralLatencySamples, int64_t neuralDelaySamples,
                 int neuralSpacingSamples) {
        (void) neuralDelaySamples;
        (void) neuralSpacingSamples;
        sr = std::max(8000.0, sampleRate);
        ch = std::max(1, numChannels);
        warper.prepare(sr);
        lfTable.prepare();

        fftSize = warper.fftLength();
        halfBins = warper.numBins();
        hop = std::max(64, fftSize / 8); // 8x overlap for artifact-free WOLA
        binHz = warper.hzPerBin();

        // Low-latency sample-exact alignment with Signalsmith Stretch
        coreAlignLat = std::max(0, spectralLatencySamples);
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

        // STFT input/output rings per channel
        stftMask = fftSize * 2 - 1; // 2*N power-of-2
        dryInRing.assign((size_t) ch, std::vector<float>((size_t) (fftSize * 2), 0.0f));
        specInRing.assign((size_t) ch, std::vector<float>((size_t) (fftSize * 2), 0.0f));
        outRing.assign((size_t) ch, std::vector<float>((size_t) (fftSize * 2), 0.0f));
        lfInRing.assign((size_t) (fftSize * 2), 0.0f);
        upperInRing.assign((size_t) (fftSize * 2), 0.0f);
        subInRing.assign((size_t) (fftSize * 2), 0.0f);
        breathInRing.assign((size_t) (fftSize * 2), 0.0f);

        // Spectral scratch buffers (allocated here, zero audio-thread allocation)
        fftWork.assign((size_t) fftSize, {});
        lfWork.assign((size_t) fftSize, {});
        upperWork.assign((size_t) fftSize, {});
        subWork.assign((size_t) fftSize, {});
        breathWork.assign((size_t) fftSize, {});
        specChanWork.assign((size_t) fftSize, {});
        dryChanWork.assign((size_t) fftSize, {});
        outChanWork.assign((size_t) fftSize, {});
        minPhaseFilter.assign((size_t) halfBins, {1.0f, 0.0f});

        magDryMono.assign((size_t) halfBins, 0.0f);
        magSpecMono.assign((size_t) halfBins, 0.0f);
        envDryFull.assign((size_t) halfBins, 0.0f);
        tiltDry.assign((size_t) halfBins, 1.0f);
        envSpecFull.assign((size_t) halfBins, 0.0f);
        pureTractSpec.assign((size_t) halfBins, 1.0f);
        tiltSpec.assign((size_t) halfBins, 1.0f);
        warpedTract.assign((size_t) halfBins, 1.0f);
        neuralGain.assign((size_t) halfBins, 1.0f);
        totalGain.assign((size_t) halfBins, 1.0f);
        consGain.assign((size_t) halfBins, 1.0f);

        timbre.prepare(sr, fftSize);
        reset();
    }

    void reset() {
        for (auto& v : dryAlignRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : dryInRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : specInRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : outRing) std::fill(v.begin(), v.end(), 0.0f);
        std::fill(lfInRing.begin(), lfInRing.end(), 0.0f);
        std::fill(upperInRing.begin(), upperInRing.end(), 0.0f);
        std::fill(subInRing.begin(), subInRing.end(), 0.0f);
        std::fill(breathInRing.begin(), breathInRing.end(), 0.0f);

        neuralTimeline.reset();
        yinTimeline.reset();
        sampleClock = 0;
        hopCounter = 0;
        tremorPhase = 0.0;
        rngState = 0x9E3779B9u;
        jitterSmooth = 0.0f;

        dcState = hpPrePrevIn = hpPrePrevOut = 0.0f;
        f0SvfIc1 = f0SvfIc2 = f0Svf2Ic1 = f0Svf2Ic2 = 0.0f;
        prevFundBp = 0.0f;
        periodCounter = 0;
        sub2SvfIc1 = sub2SvfIc2 = 0.0f;
        sub3SvfIc1 = sub3SvfIc2 = 0.0f;
        midLpHi = midLpLo = 0.0f;
        midEnv = 0.02f;
        upHpIc1 = upHpIc2 = 0.0f;
        breathPink = breathHpIc1 = breathHpIc2 = 0.0f;
        rmsFast = rmsTail = 0.0f;

        smoothVoiced = 0.0f;
        smoothSibilant = 0.0f;
        smoothTargetF0 = 140.0f;
        smoothLfGain = 1.0f;
        smoothUpGain = 0.5f;
        smoothSubGain = 0.5f;
        smoothBreathGain = 0.5f;
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
        const float conf = e.confidence > 0.0f ? e.confidence : (e.voiced() ? std::clamp(1.0f - e.aperiodicity / 0.45f, 0.0f, 1.0f) : 0.0f);
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
            const int sIdx = (int) (sampleClock & stftMask);

            float alignedSpecMono = 0.0f;
            for (int c = 0; c < ch; ++c) {
                dryAlignRing[(size_t) c][(size_t) wIdx] = rawInput[c][i];
                const float dSamp = sampleClock >= coreAlignLat ? dryAlignRing[(size_t) c][(size_t) dryRIdx] : 0.0f;
                const float sSamp = specAudio[c][i];

                dryInRing[(size_t) c][(size_t) sIdx] = dSamp;
                specInRing[(size_t) c][(size_t) sIdx] = sSamp;
                alignedSpecMono += sSamp;

                // Read previously overlap-added STFT output and clear cell for future OLA
                specAudio[c][i] = outRing[(size_t) c][(size_t) sIdx];
                outRing[(size_t) c][(size_t) sIdx] = 0.0f;
            }
            alignedSpecMono /= (float) ch;

            // Advance phase-locked excitation generators synchronized 1:1 with specAudio
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

    static void stepTptSvf(float x, float fc, float srHz, float q, float& ic1, float& ic2, float& outLp, float& outBp, float& outHp) {
        const float normFc = std::clamp(fc / srHz, 0.0004f, 0.44f);
        const float g = std::tan((float) M_PI * normFc);
        const float k = 1.0f / std::max(0.25f, q);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        outBp = v1;
        outLp = v2;
        outHp = x - k * v1 - v2;
    }

    void synthesizeExcitationSample(int sIdx, float alignedSpecMono, const HybridParams& p) {
        const float srF = (float) sr;
        const float agePos = std::clamp(p.age / 50.0f, 0.0f, 1.0f);
        tremorPhase += 5.4 / sr;
        if (tremorPhase >= 1.0) tremorPhase -= 1.0;

        jitterSmooth += (nextNoise() - jitterSmooth) * (1.0f - (float) std::exp(-1.0 / (0.004 * sr)));
        const float ageMod = agePos > 0.02f ? agePos * agePos * (0.12f * (float) std::sin(2.0 * M_PI * tremorPhase) + 0.06f * jitterSmooth) : 0.0f;
        const float targetF0 = std::clamp(smoothTargetF0 * std::pow(2.0f, ageMod / 12.0f), 42.0f, 1150.0f);

        // Track syllable RMS and tail envelope cleanly for breath/growl dynamics
        const float absIn = std::abs(alignedSpecMono);
        const float rmsAtt = 1.0f - (float) std::exp(-1.0 / (0.020 * sr));
        const float rmsRel = 1.0f - (float) std::exp(-1.0 / (0.110 * sr));
        rmsFast += (absIn - rmsFast) * rmsAtt;
        rmsTail += (absIn - rmsTail) * (absIn > rmsTail ? rmsAtt : rmsRel);
        const float sylEnv = std::max(rmsFast, rmsTail * 0.72f);

        // 1. Pre-condition signal (remove DC + sub-F0 rumble) before TPT quadrature fundamental SVF
        dcState += (alignedSpecMono - dcState) * (1.0f - (float) std::exp(-2.0 * M_PI * 22.0 / sr));
        const float noDc = alignedSpecMono - dcState;
        const float hpAlpha = (float) std::exp(-2.0 * M_PI * (0.62f * targetF0) / srF);
        const float preHp = hpAlpha * (hpPrePrevOut + noDc - hpPrePrevIn);
        hpPrePrevIn = noDc;
        hpPrePrevOut = preHp;

        float f0Lp = 0.0f, f0Bp = 0.0f, f0Hp = 0.0f;
        constexpr float kF0Q1 = 2.2f;
        stepTptSvf(preHp, targetF0, srF, kF0Q1, f0SvfIc1, f0SvfIc2, f0Lp, f0Bp, f0Hp);
        const float fundBp1 = f0Bp / kF0Q1;

        float f0Lp2 = 0.0f, f0Bp2 = 0.0f, f0Hp2 = 0.0f;
        constexpr float kF0Q2 = 1.4f;
        stepTptSvf(fundBp1, targetF0, srF, kF0Q2, f0Svf2Ic1, f0Svf2Ic2, f0Lp2, f0Bp2, f0Hp2);
        const float fundBp = f0Bp2 / kF0Q2;
        const float fundLp = f0Lp2 / kF0Q2; // -90 deg quadrature with >18 dB H2 rejection

        // Detect positive-going fundamental zero crossings for C0-continuous subharmonic period flips
        if (prevFundBp <= 0.0f && fundBp > 0.0f) ++periodCounter;
        prevFundBp = fundBp;

        // Analytic instantaneous phase theta & envelope A_F0 locked 100% to alignedSpecMono
        const float ampF0 = std::hypot(fundBp, fundLp);
        float phase01 = 0.0f;
        if (ampF0 > 1e-6f) {
            float theta = std::atan2(fundLp, fundBp);
            if (theta < 0.0f) theta += 2.0f * (float) M_PI;
            phase01 = theta * (1.0f / (2.0f * (float) M_PI));
        }

        // 2. Phase-locked band-limited LF glottal pulse train
        const float glottalEff = std::clamp(p.glottal + agePos * 0.15f, -1.0f, 1.0f);
        const float f0Norm = targetF0 / srF;
        const float lfRaw = lfTable.sample(phase01, glottalEff, f0Norm);
        const float gateF0 = ampF0 > 1e-4f ? smoothVoiced : 0.0f;
        float glotSample = lfRaw * ampF0 * gateF0;
        if (agePos > 0.02f) glotSample *= (1.0f + 0.08f * agePos * jitterSmooth);
        lfInRing[(size_t) sIdx] = glotSample;

        // 3. Continuous zero-crossing F0/2 and F0/3 subharmonics (zero step clicks, zero beating)
        const float sub2Sign = (periodCounter & 1u) ? -1.0f : 1.0f;
        const uint32_t m3 = periodCounter % 3u;
        const float sub3Sign = (m3 == 0u) ? 1.0f : (m3 == 1u ? -1.0f : 0.0f);
        float s2Lp = 0.0f, s2Bp = 0.0f, s2Hp = 0.0f;
        float s3Lp = 0.0f, s3Bp = 0.0f, s3Hp = 0.0f;
        stepTptSvf(fundBp * sub2Sign, targetF0 * 0.5f, srF, 1.6f, sub2SvfIc1, sub2SvfIc2, s2Lp, s2Bp, s2Hp);
        stepTptSvf(fundBp * sub3Sign, targetF0 / 3.0f, srF, 1.6f, sub3SvfIc1, sub3SvfIc2, s3Lp, s3Bp, s3Hp);
        subInRing[(size_t) sIdx] = (0.72f * s2Bp + 0.34f * s3Bp) * gateF0;

        // 4. Mid-band (300..2800 Hz) DC-balanced asymmetric soft-clipper -> >2.2 kHz upper harmonic exciter
        const float aHi = 1.0f - (float) std::exp(-2.0 * M_PI * 2800.0 / sr);
        const float aLo = 1.0f - (float) std::exp(-2.0 * M_PI * 300.0 / sr);
        midLpHi += (noDc - midLpHi) * aHi;
        midLpLo += (noDc - midLpLo) * aLo;
        const float midBand = midLpHi - midLpLo;
        const float absMid = std::abs(midBand);
        midEnv += (absMid - midEnv) * (absMid > midEnv ? rmsAtt : rmsRel);
        const float midNorm = midBand / std::max(0.015f, midEnv * 1.9f);
        const float shaped = std::tanh(midNorm * 1.45f + 0.32f * midNorm * midNorm);
        float upLp = 0.0f, upBp = 0.0f, upHp = 0.0f;
        stepTptSvf(shaped * midEnv * 1.6f, 2200.0f, srF, 0.7071f, upHpIc1, upHpIc2, upLp, upBp, upHp);
        upperInRing[(size_t) sIdx] = upHp * gateF0;

        // 5. Pitch-synchronous velvet breath: 900 Hz HPF pink noise gated by glottal open phase + syllable envelope
        const float white = nextNoise();
        breathPink += (white - breathPink) * (1.0f - (float) std::exp(-2.0 * M_PI * 3400.0 / sr));
        float bLp = 0.0f, bBp = 0.0f, bHp = 0.0f;
        stepTptSvf(breathPink, 900.0f, srF, 0.7071f, breathHpIc1, breathHpIc2, bLp, bBp, bHp);
        const float oq = 0.32f + 0.50f * (glottalEff + 1.0f) * 0.5f;
        const float openEnv = phase01 < oq ? std::sin((float) M_PI * phase01 / std::max(0.05f, oq)) : 0.0f;
        const float breathGate = sylEnv > 0.0025f ? std::clamp((sylEnv - 0.0025f) / 0.015f, 0.0f, 1.0f) : 0.0f;
        breathInRing[(size_t) sIdx] = bHp * (0.40f + 0.60f * openEnv) * breathGate * std::max(smoothVoiced, 0.25f * breathGate);
    }

    void processFrame(const HybridParams& p, bool useNeural) {
        const int64_t frameEnd = sampleClock;
        const int64_t frameCentreInInput = frameEnd - coreAlignLat - fftSize / 2;

        // 1. Lookup sample-aligned YIN estimate (primary instantaneous) & neural estimate (octave validation)
        const auto ye = yinTimeline.query(frameCentreInInput);
        const auto ne = neuralTimeline.query(frameCentreInInput);
        float f0In = ye.f0 > 42.0f ? ye.f0 : (useNeural && ne.f0 > 42.0f ? ne.f0 : 0.0f);
        if (useNeural && ye.f0 > 42.0f && ne.f0 > 42.0f && std::abs(ne.time - frameCentreInInput) < (int64_t) (sr * 0.22)) {
            const float ratio = ne.f0 / ye.f0;
            if (std::abs(ratio - 0.5f) < 0.09f || std::abs(ratio - 2.0f) < 0.16f) f0In = ne.f0;
        }
        const float trackerConf = ye.f0 > 42.0f ? ye.confidence : (useNeural ? ne.confidence : ye.confidence);
        const float aperiodicity = std::min(ye.aperiodicity, ne.aperiodicity);

        // 2. Analyze aligned dry frame solely for speaker profile learning and >3.5 kHz unvoiced sibilant gating
        double drySq = 0.0;
        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((frameEnd - fftSize + m) & stftMask);
            float s = 0.0f;
            for (int c = 0; c < ch; ++c) s += dryInRing[(size_t) c][(size_t) rIdx];
            s /= (float) ch;
            drySq += (double) s * s;
            fftWork[(size_t) m] = {s * hannWin[(size_t) m], 0.0f};
        }
        const float frameRms = (float) std::sqrt(drySq / fftSize);
        fft(fftWork, false);

        const int bin100 = std::max(1, (int) (100.0f / binHz));
        const int bin1200 = std::max(bin100 + 1, (int) (1200.0f / binHz));
        const int bin3500 = std::min(halfBins - 1, (int) (3500.0f / binHz));
        const int bin4800 = std::min(halfBins - 1, std::max(bin3500 + 1, (int) (4800.0f / binHz)));
        const int bin12k = std::min(halfBins - 1, (int) (12000.0f / binHz));

        double eLow = 0.0, eHigh = 0.0;
        for (int k = 0; k < halfBins; ++k) {
            const float m = std::abs(fftWork[(size_t) k]);
            magDryMono[(size_t) k] = m;
            if (k >= bin100 && k <= bin1200) eLow += (double) m * m;
            if (k >= bin3500 && k <= bin12k) eHigh += (double) m * m;
        }
        const float hfRatio = (float) (eHigh / (eLow + 1e-9));

        // 3. Gather aligned shifted mono frame for true source-filter separation on specAudio
        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((frameEnd - fftSize + m) & stftMask);
            float s = 0.0f;
            for (int c = 0; c < ch; ++c) s += specInRing[(size_t) c][(size_t) rIdx];
            s /= (float) ch;
            fftWork[(size_t) m] = {s * hannWin[(size_t) m], 0.0f};
        }
        fft(fftWork, false);
        for (int k = 0; k < halfBins; ++k) magSpecMono[(size_t) k] = std::abs(fftWork[(size_t) k]);

        // Continuous voicing mask + strictly unvoiced sibilant guard mask
        const float rawTrackerVoiced =
            frameRms < 0.0012f ? 0.0f : std::clamp((trackerConf - 0.25f) / 0.35f, 0.0f, 1.0f) * std::clamp(1.15f - 0.75f * aperiodicity, 0.0f, 1.0f);
        const float hfSuppress = hfRatio > 1.8f ? std::clamp((hfRatio - 1.8f) / 2.2f, 0.0f, 0.85f) : 0.0f;
        const float targetVoiced = rawTrackerVoiced * (1.0f - hfSuppress);
        const float attackCoef = targetVoiced > smoothVoiced ? 0.45f : 0.18f;
        smoothVoiced += (targetVoiced - smoothVoiced) * attackCoef;

        const float rawSibGate = (p.consonantGuard && frameRms > 0.0015f && hfRatio > 0.45f && rawTrackerVoiced < 0.72f)
                                     ? std::clamp((hfRatio - 0.45f) / 0.85f, 0.0f, 1.0f) *
                                           std::clamp((0.72f - rawTrackerVoiced) / 0.45f, 0.0f, 1.0f)
                                     : 0.0f;
        smoothSibilant += (rawSibGate - smoothSibilant) * (rawSibGate > smoothSibilant ? 0.75f : 0.35f);

        // 4. Update smooth target F0 for phase-locked SVF
        const float pitchRatio = std::pow(2.0f, p.pitchSemitones / 12.0f);
        if (f0In > 42.0f && rawTrackerVoiced > 0.25f) {
            float desiredF0 = std::clamp(f0In * pitchRatio, 45.0f, 1100.0f);
            if (p.steadyPitch && p.speakerMedianF0 > 40.0f) {
                const float targetCentre = std::clamp(p.speakerMedianF0 * pitchRatio, 45.0f, 1100.0f);
                desiredF0 = std::exp(std::log(desiredF0) * 0.75f + std::log(targetCentre) * 0.25f);
            }
            smoothTargetF0 += (desiredF0 - smoothTargetF0) * 0.50f;
        }

        // 5. Pure low-quefrency cepstral source-filter separation directly from specAudio + dry tilt/speaker learning
        warper.extractEnvelope(magDryMono.data(), f0In, envDryFull.data(), nullptr, tiltDry.data());
        timbre.learnSpeaker(envDryFull.data(), smoothVoiced);

        warper.extractEnvelope(magSpecMono.data(), smoothTargetF0, envSpecFull.data(), pureTractSpec.data(), tiltSpec.data());

        const float activeMix = p.characterIndex > 0
                                    ? 1.0f
                                    : std::clamp(p.regen + std::abs(p.formantSemitones) / 4.0f + std::abs(p.pitchSemitones) / 8.0f +
                                                     std::abs(p.glottal) + p.growl,
                                                 0.0f, 1.0f);
        const float sharpness = activeMix * std::clamp(0.30f + 0.35f * p.regen + 0.25f * std::abs(p.formantSemitones) / 8.0f, 0.15f, 0.85f);
        warper.warpEnvelope(pureTractSpec.data(), p.formantSemitones, sharpness, warpedTract.data());

        float hnrBias = 0.0f;
        timbre.evaluate(warpedTract.data(), p, neuralGain.data(), hnrBias);

        // Compute total spectral gain: non-linear formant transfer * neural Bark timbre * H1-H2 glottal shaping
        const float regenAmt = std::clamp(p.regen, 0.0f, 1.0f);
        const float neuralAmt = 0.35f + 0.65f * regenAmt;
        const float headWeight = std::clamp(std::max(0.0f, p.glottal) * 0.75f + std::clamp((p.pitchSemitones - 1.5f) / 10.0f, 0.0f, 1.0f) * 0.45f, 0.0f, 1.0f);
        const float chestWeight = std::clamp(std::max(0.0f, -p.glottal) * 0.75f + std::clamp((-p.pitchSemitones - 1.5f) / 10.0f, 0.0f, 1.0f) * 0.45f, 0.0f, 1.0f);
        const float f0Ref = std::clamp(smoothTargetF0, 50.0f, 550.0f);
        const float fSub = std::max(42.0f, 0.72f * f0Ref);

        for (int k = 0; k < halfBins; ++k) {
            const float f = std::max(20.0f, (float) k * binHz);
            const float tractTransfer = std::clamp(warpedTract[(size_t) k] / std::max(1e-4f, pureTractSpec[(size_t) k]), 0.16f, 6.0f);
            const float neuralTerm = std::pow(std::clamp(neuralGain[(size_t) k], 0.2f, 4.0f), neuralAmt);

            float glotDb = 0.0f;
            if (headWeight > 0.005f) {
                const float octF0 = std::log2(std::max(30.0f, f) / f0Ref);
                glotDb += regenAmt * headWeight * 3.0f * std::exp(-octF0 * octF0 / 0.22f);
                const float octGlare = std::log2(std::max(30.0f, f) / 2900.0f);
                glotDb -= regenAmt * headWeight * 2.2f * std::exp(-octGlare * octGlare / 0.45f);
            }
            if (chestWeight > 0.005f) {
                const float octChest = std::log2(std::max(30.0f, f) / 130.0f);
                glotDb += regenAmt * chestWeight * 2.8f * std::exp(-octChest * octChest / 0.55f);
                const float octBox = std::log2(std::max(30.0f, f) / 450.0f);
                glotDb -= regenAmt * chestWeight * 1.4f * std::exp(-octBox * octBox / 0.30f);
                const float octPres = std::log2(std::max(30.0f, f) / 4200.0f);
                glotDb += regenAmt * chestWeight * 2.0f * std::exp(-octPres * octPres / 0.75f);
            }
            if (f < fSub) glotDb -= 5.0f * (1.0f - f / fSub);
            const float glotTerm = std::pow(10.0f, glotDb / 20.0f);

            totalGain[(size_t) k] = std::clamp(tractTransfer * neuralTerm * glotTerm, 0.12f, 7.0f);

            // Subtle high-frequency sibilant tilt compensation (>3.5 kHz unshifted guard only)
            const float sibTiltDb = std::clamp(p.formantSemitones * 0.22f, -1.8f, 1.8f) * activeMix;
            consGain[(size_t) k] = std::pow(10.0f, sibTiltDb / 20.0f);
        }

        // Convert totalGain magnitude curve into causal minimum-phase complex filter (zero pre-echo)
        warper.computeMinimumPhaseFilter(totalGain.data(), minPhaseFilter.data());

        // 6. FFT of the four phase-locked excitation streams
        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((frameEnd - fftSize + m) & stftMask);
            const float win = hannWin[(size_t) m];
            lfWork[(size_t) m] = {lfInRing[(size_t) rIdx] * win, 0.0f};
            upperWork[(size_t) m] = {upperInRing[(size_t) rIdx] * win, 0.0f};
            subWork[(size_t) m] = {subInRing[(size_t) rIdx] * win, 0.0f};
            breathWork[(size_t) m] = {breathInRing[(size_t) rIdx] * win, 0.0f};
        }
        fft(lfWork, false);
        fft(upperWork, false);
        fft(subWork, false);
        fft(breathWork, false);

        const int bin650 = std::max(2, (int) (650.0f / binHz));
        const int bin800 = std::max(bin650 + 1, (int) (800.0f / binHz));
        const int bin2000 = std::max(bin800 + 1, (int) (2000.0f / binHz));
        const int bin4500 = std::min(halfBins - 1, (int) (4500.0f / binHz));

        const float breathMix = std::clamp(p.breath + std::max(0.0f, hnrBias) * 0.10f, 0.0f, 1.0f) * (0.35f + 0.65f * regenAmt);
        const float effortBoost = std::clamp(0.65f + 0.50f * p.effort, 0.45f, 1.45f);
        const float growlMix = std::clamp(p.growl * effortBoost, 0.0f, 1.0f) * (0.35f + 0.65f * regenAmt) * smoothVoiced;
        const float downFactor = p.pitchSemitones < -0.5f ? std::clamp(-p.pitchSemitones / 10.0f, 0.15f, 1.0f) : 0.15f;
        const float voicedMorph = 0.35f * regenAmt * smoothVoiced * smoothVoiced;

        // 7. Per-channel causal minimum-phase filtering & distortion-free spectral fusion
        for (int c = 0; c < ch; ++c) {
            for (int m = 0; m < fftSize; ++m) {
                const int rIdx = (int) ((frameEnd - fftSize + m) & stftMask);
                specChanWork[(size_t) m] = {specInRing[(size_t) c][(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
                dryChanWork[(size_t) m] = {dryInRing[(size_t) c][(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
            }
            fft(specChanWork, false);
            fft(dryChanWork, false);

            // Compute matched reference powers against filtered Signalsmith signal
            double specPow = 0.0, lfPow = 0.0, upPow = 0.0, subPow = 0.0, breathPow = 0.0;
            double dryConsPow = 0.0, warpedConsPow = 0.0;
            for (int k = 1; k < halfBins; ++k) {
                const float yMag = std::abs(specChanWork[(size_t) k]) * totalGain[(size_t) k];
                if (k <= bin4500) {
                    specPow += (double) yMag * yMag;
                    const float ml = std::abs(lfWork[(size_t) k]) * envSpecFull[(size_t) k] * totalGain[(size_t) k];
                    lfPow += (double) ml * ml;
                }
                if (k <= bin650) {
                    const float ms = std::abs(subWork[(size_t) k]) * totalGain[(size_t) k];
                    subPow += (double) ms * ms;
                }
                if (k >= bin2000 && k <= bin12k) {
                    const float excEnvK = std::clamp(warpedTract[(size_t) k] * tiltDry[(size_t) k], 1e-4f, 14.0f);
                    const float mu = std::abs(upperWork[(size_t) k]) * excEnvK;
                    upPow += (double) mu * mu;
                }
                if (k >= bin800 && k <= bin12k) {
                    const float excEnvK = std::clamp(warpedTract[(size_t) k] * tiltDry[(size_t) k], 1e-4f, 14.0f);
                    const float mb = std::abs(breathWork[(size_t) k]) * excEnvK;
                    breathPow += (double) mb * mb;
                }
                if (k >= bin3500) {
                    const float dm = std::abs(dryChanWork[(size_t) k]);
                    const float cm = dm * consGain[(size_t) k];
                    dryConsPow += (double) dm * dm;
                    warpedConsPow += (double) cm * cm;
                }
            }

            const float refOk = specPow > 1e-7 ? 1.0f : 0.0f;
            const float tgtLfGain = (refOk > 0.0f && lfPow > 1e-10) ? std::clamp((float) std::sqrt(specPow / lfPow), 0.0f, 2.5f) : 0.0f;
            const float tgtUpGain = (refOk > 0.0f && upPow > 1e-10) ? std::clamp((float) std::sqrt((specPow * 0.22) / upPow), 0.0f, 1.4f) : 0.0f;
            const float tgtSubGain = (refOk > 0.0f && subPow > 1e-10) ? std::clamp((float) std::sqrt((specPow * 0.38) / subPow), 0.0f, 1.6f) : 0.0f;
            const float tgtBreathGain = (refOk > 0.0f && breathPow > 1e-10) ? std::clamp((float) std::sqrt((specPow * 0.16) / breathPow), 0.0f, 1.8f) : 0.0f;

            if (c == 0) {
                smoothLfGain += (tgtLfGain - smoothLfGain) * 0.35f;
                smoothUpGain += (tgtUpGain - smoothUpGain) * 0.35f;
                smoothSubGain += (tgtSubGain - smoothSubGain) * 0.35f;
                smoothBreathGain += (tgtBreathGain - smoothBreathGain) * 0.35f;
            }
            const float lfGain = c == 0 ? smoothLfGain : tgtLfGain;
            const float upGain = (c == 0 ? smoothUpGain : tgtUpGain) * regenAmt * smoothVoiced * downFactor;
            const float subGain = (c == 0 ? smoothSubGain : tgtSubGain) * growlMix;
            const float breathGain = (c == 0 ? smoothBreathGain : tgtBreathGain) * breathMix;
            const float consScale = warpedConsPow > 1e-10 ? std::clamp((float) std::sqrt(dryConsPow / warpedConsPow), 0.5f, 2.0f) : 1.0f;

            double preExcPow = 0.0, postExcPow = 0.0;
            for (int k = 0; k < halfBins; ++k) {
                const std::complex<float> mp = minPhaseFilter[(size_t) k];
                std::complex<float> yBase = specChanWork[(size_t) k] * mp;

                // Comb-free, zero-phase-cancellation LF magnitude shaping inside voiced harmonic band
                if (k >= 1 && k <= bin4500 && voicedMorph > 0.01f) {
                    const float magSpec = std::abs(yBase);
                    const float magLf = std::abs(lfWork[(size_t) k]) * envSpecFull[(size_t) k] * totalGain[(size_t) k] * lfGain;
                    if (magSpec > 1e-6f && magLf > 1e-6f) {
                        const float magGoal = magSpec * (1.0f - voicedMorph) + magLf * voicedMorph;
                        const float ratio = std::clamp(magGoal / magSpec, 0.55f, 1.85f);
                        yBase *= ratio;
                    }
                }
                if (k >= 1 && k <= bin4500) preExcPow += (double) std::norm(yBase);

                // Additive phase-locked upper harmonics & velvet breath shaped by full-bandwidth warpedTract * tiltDry
                const float excEnvK = std::clamp(warpedTract[(size_t) k] * tiltDry[(size_t) k], 1e-4f, 14.0f);
                const std::complex<float> unitMp = mp / std::max(1e-4f, totalGain[(size_t) k]);
                std::complex<float> yVoiced = yBase;
                yVoiced += upperWork[(size_t) k] * unitMp * (excEnvK * upGain);
                if (k <= bin650) yVoiced += subWork[(size_t) k] * mp * subGain;
                yVoiced += breathWork[(size_t) k] * unitMp * (excEnvK * breathGain);

                if (k >= 1 && k <= bin4500) postExcPow += (double) std::norm(yVoiced);

                // Consonant Guard: STRICTLY unvoiced AND STRICTLY > 3.5 kHz (zero original-voice bleed!)
                float unshiftedMix = 0.0f;
                if (k > bin3500 && smoothSibilant > 0.002f) {
                    float hf = 1.0f;
                    if (k < bin4800) {
                        const float u = (float) (k - bin3500) / (float) (bin4800 - bin3500);
                        hf = 0.5f - 0.5f * std::cos((float) M_PI * u);
                    }
                    unshiftedMix = smoothSibilant * hf;
                }
                const std::complex<float> yCons = dryChanWork[(size_t) k] * (consGain[(size_t) k] * consScale);
                const std::complex<float> yOut = unshiftedMix > 0.001f ? (yVoiced * (1.0f - unshiftedMix) + yCons * unshiftedMix) : yVoiced;

                outChanWork[(size_t) k] = yOut;
                if (k > 0 && k < fftSize / 2) outChanWork[(size_t) (fftSize - k)] = std::conj(yOut);
            }

            // Gentle energy balance guard so turning up Regen/Growl never overdrives
            if (postExcPow > preExcPow * 1.15 && postExcPow > 1e-8) {
                const float normScale = std::clamp((float) std::sqrt((preExcPow * 1.15) / postExcPow), 0.72f, 1.0f);
                for (int k = 0; k < fftSize; ++k) outChanWork[(size_t) k] *= normScale;
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
        status.consonant = smoothSibilant;
        status.regen = regenAmt * smoothVoiced;
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
    int coreAlignLat = 1536, totalLatency = 3584;
    int delayMask = 0, stftMask = 0;

    VocalTractWarper warper;
    LFGlottalTable lfTable;
    NeuralTimbreShaper timbre;
    PitchTimeline neuralTimeline, yinTimeline;

    std::vector<float> hannWin, synthWin;
    std::vector<std::complex<float>> twFwd, twInv;
    std::vector<std::vector<float>> dryAlignRing, dryInRing, specInRing, outRing;
    std::vector<float> lfInRing, upperInRing, subInRing, breathInRing;

    std::vector<float> magDryMono, magSpecMono, envDryFull, tiltDry, envSpecFull, pureTractSpec, tiltSpec;
    std::vector<float> warpedTract, neuralGain, totalGain, consGain;
    std::vector<std::complex<float>> fftWork, lfWork, upperWork, subWork, breathWork, specChanWork, dryChanWork, outChanWork;
    std::vector<std::complex<float>> minPhaseFilter;

    int64_t sampleClock = 0;
    int hopCounter = 0;
    double tremorPhase = 0.0;
    uint32_t rngState = 0x9E3779B9u;
    float jitterSmooth = 0.0f;

    float dcState = 0.0f, hpPrePrevIn = 0.0f, hpPrePrevOut = 0.0f;
    float f0SvfIc1 = 0.0f, f0SvfIc2 = 0.0f, f0Svf2Ic1 = 0.0f, f0Svf2Ic2 = 0.0f, prevFundBp = 0.0f;
    uint32_t periodCounter = 0;
    float sub2SvfIc1 = 0.0f, sub2SvfIc2 = 0.0f, sub3SvfIc1 = 0.0f, sub3SvfIc2 = 0.0f;
    float midLpHi = 0.0f, midLpLo = 0.0f, midEnv = 0.02f;
    float upHpIc1 = 0.0f, upHpIc2 = 0.0f;
    float breathPink = 0.0f, breathHpIc1 = 0.0f, breathHpIc2 = 0.0f;
    float rmsFast = 0.0f, rmsTail = 0.0f;

    float smoothVoiced = 0.0f, smoothSibilant = 0.0f, smoothTargetF0 = 140.0f;
    float smoothLfGain = 1.0f, smoothUpGain = 0.5f, smoothSubGain = 0.5f, smoothBreathGain = 0.5f;
    HybridStatus status{};
};

} // namespace adub
