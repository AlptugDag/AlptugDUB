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
 * Direct Glottal-Residual & Minimum-Phase Vocal-Tract Voice Regeneration Engine:
 *  - Stage 1: Causal minimum-phase inverse filtering (1024-pt STFT, 128-sample / 2.7 ms hop)
 *             extracts clean whitened glottal excitation free of source formant coloration.
 *  - Stage 2: Time-domain GCI-locked glottal pulse regenerator resynthesizes single un-stretched
 *             glottal pulses at target F0, adding band-limited LF shape, diplophonic subharmonics,
 *             and open-phase breath; unvoiced consonants reconstruct with 100% unity transparency.
 *  - Stage 3: Causal minimum-phase target vocal tract re-filtering applies non-linear F1/F2/F3/F4
 *             warping, 24-Bark Neural Timbre Shaper, and H1-H2 glottal curves with zero pre-echo.
 */
class HybridRegenerator {
public:
    void prepare(double sampleRate, int numChannels, int maxBlock, int /*unusedSpecLat*/ = 0,
                 int64_t /*unusedNeuralDelay*/ = 0, int /*unusedNeuralSpace*/ = 0) {
        sr = std::max(8000.0, sampleRate);
        ch = std::max(1, numChannels);
        warper.prepare(sr);
        lfTable.prepare();

        fftSize = warper.fftLength();
        halfBins = warper.numBins();
        hop = std::max(32, fftSize / 8); // 8x overlap (128 samples = ~2.7 ms at 44.1/48 kHz)
        binHz = warper.hzPerBin();

        readLookback = (fftSize * 3) / 4;
        writeMargin = fftSize / 2;
        maxHalfWidth = fftSize / 2 - 16;
        stage3DryDelay = fftSize + readLookback + writeMargin;
        totalLatency = stage3DryDelay + fftSize;

        // Hann windows + unity 8x WOLA normalization (sum of Hann^2 at 8x overlap = 3.0)
        hannWin.resize((size_t) fftSize);
        synthWin.resize((size_t) fftSize);
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

        // Single consistent power-of-2 ring size covering Stage 1 -> Stage 2 -> Stage 3 + maxBlock
        int hSize = 1;
        while (hSize < totalLatency + maxBlock + fftSize * 4) hSize <<= 1;
        histSize = hSize;
        histMask = hSize - 1;

        dryInRing.assign((size_t) ch, std::vector<float>((size_t) histSize, 0.0f));
        resOutRing.assign((size_t) ch, std::vector<float>((size_t) histSize, 0.0f));
        resHist.assign((size_t) ch, std::vector<float>((size_t) histSize, 0.0f));
        resLpHist.assign((size_t) histSize, 0.0f);
        resRegenRing.assign((size_t) ch, std::vector<float>((size_t) histSize, 0.0f));
        breathSrcRing.assign((size_t) histSize, 0.0f);
        breathRegenRing.assign((size_t) histSize, 0.0f);
        outRing.assign((size_t) ch, std::vector<float>((size_t) histSize, 0.0f));

        // Spectral scratch buffers (preallocated, zero audio-thread allocation)
        fftWork.assign((size_t) fftSize, {});
        dryChanWork.assign((size_t) fftSize, {});
        resWork.assign((size_t) fftSize, {});
        regenChanWork.assign((size_t) fftSize, {});
        breathWork.assign((size_t) fftSize, {});
        outChanWork.assign((size_t) fftSize, {});

        inTractMp.assign((size_t) halfBins, {1.0f, 0.0f});
        targetTractMp.assign((size_t) halfBins, {1.0f, 0.0f});

        magDryMono1.assign((size_t) halfBins, 0.0f);
        envDry1.assign((size_t) halfBins, 1.0f);
        tiltDry1.assign((size_t) halfBins, 1.0f);
        invNorm1.assign((size_t) halfBins, 1.0f);

        magDryMono3.assign((size_t) halfBins, 0.0f);
        envDry3.assign((size_t) halfBins, 1.0f);
        pureTract3.assign((size_t) halfBins, 1.0f);
        tiltDry3.assign((size_t) halfBins, 1.0f);
        warpedTract3.assign((size_t) halfBins, 1.0f);
        neuralGain3.assign((size_t) halfBins, 1.0f);
        targetTractMag.assign((size_t) halfBins, 1.0f);

        timbre.prepare(sr, fftSize);
        reset();
    }

    void reset() {
        for (auto& v : dryInRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : resOutRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : resHist) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : resRegenRing) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : outRing) std::fill(v.begin(), v.end(), 0.0f);
        std::fill(resLpHist.begin(), resLpHist.end(), 0.0f);
        std::fill(breathSrcRing.begin(), breathSrcRing.end(), 0.0f);
        std::fill(breathRegenRing.begin(), breathRegenRing.end(), 0.0f);

        neuralTimeline.reset();
        yinTimeline.reset();
        sampleClock = 0;
        hopCounter = 0;
        tremorPhase = 0.0;
        rngState = 0x9E3779B9u;

        grainCountdown = 0.0f;
        trackedGci = 0.0;
        wasVoicedGrain = false;
        gciPolarity = 1.0f;
        grainEpoch = 0;

        resLpIc1 = resLpIc2 = 0.0f;
        breathPink = breathHpIc1 = breathHpIc2 = 0.0f;
        rmsFast = rmsTail = 0.0f;

        stage2F0 = 140.0f;
        stage2Voiced = 0.0f;
        stage3F0 = 140.0f;
        stage3Voiced = 0.0f;
        smoothSibilant = 0.0f;
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
     * Processes `n` samples directly from `rawInput` and writes regenerated audio to `outAudio`.
     */
    void process(const float* const* rawInput, float* const* outAudio, int n, const HybridParams& p, bool useNeural) {
        const float pitchRatio = std::pow(2.0f, p.pitchSemitones / 12.0f);
        const float srF = (float) sr;
        const float rmsAtt = 1.0f - (float) std::exp(-1.0 / (0.020 * sr));
        const float rmsRel = 1.0f - (float) std::exp(-1.0 / (0.110 * sr));

        for (int i = 0; i < n; ++i) {
            const int cIdx = (int) (sampleClock & histMask);
            const int clearIdx = (int) ((sampleClock + fftSize) & histMask);

            // Clear future write slots well ahead of any grain tail
            for (int c = 0; c < ch; ++c) resRegenRing[(size_t) c][(size_t) clearIdx] = 0.0f;
            breathRegenRing[(size_t) clearIdx] = 0.0f;

            float monoRaw = 0.0f;
            float resMonoOut = 0.0f;
            for (int c = 0; c < ch; ++c) {
                const float x = rawInput[c][i];
                dryInRing[(size_t) c][(size_t) cIdx] = x;
                monoRaw += x;

                // Stage 1 OLA readout into causal whitened residual history ring
                const float rSamp = resOutRing[(size_t) c][(size_t) cIdx];
                resHist[(size_t) c][(size_t) cIdx] = rSamp;
                resMonoOut += rSamp;
                resOutRing[(size_t) c][(size_t) cIdx] = 0.0f;

                // Stage 3 OLA readout to plugin output
                outAudio[c][i] = outRing[(size_t) c][(size_t) cIdx];
                outRing[(size_t) c][(size_t) cIdx] = 0.0f;
            }
            monoRaw /= (float) ch;
            resMonoOut /= (float) ch;

            // Low-passed (650 Hz) residual envelope for jitter-free, octave-locked GCI peak picking
            float lpL = 0.0f, lpB = 0.0f, lpH = 0.0f;
            stepTptSvf(resMonoOut, 650.0f, srF, 0.7071f, resLpIc1, resLpIc2, lpL, lpB, lpH);
            resLpHist[(size_t) cIdx] = lpL;

            const float absIn = std::abs(monoRaw);
            rmsFast += (absIn - rmsFast) * rmsAtt;
            rmsTail += (absIn - rmsTail) * (absIn > rmsTail ? rmsAtt : rmsRel);

            // Advance tremor oscillator & background pink high-pass noise source for voiced velvet breath
            tremorPhase += 5.4 / sr;
            if (tremorPhase >= 1.0) tremorPhase -= 1.0;

            const float white = nextNoise();
            breathPink += (white - breathPink) * (1.0f - (float) std::exp(-2.0 * M_PI * 3400.0 / sr));
            float bLp = 0.0f, bBp = 0.0f, bHp = 0.0f;
            stepTptSvf(breathPink, 900.0f, srF, 0.7071f, breathHpIc1, breathHpIc2, bLp, bBp, bHp);
            breathSrcRing[(size_t) cIdx] = bHp;

            // Stage 2: Time-domain GCI-locked glottal pulse grain scheduler
            const bool pitchActive = std::abs(p.pitchSemitones) > 0.03f;
            const bool emitVoiced = (stage2Voiced >= 0.25f) && (pitchActive || p.regen > 0.05f || p.growl > 0.01f);
            if (!emitVoiced && grainCountdown > 32.0f) grainCountdown = 32.0f;

            if (grainCountdown <= 0.0f) {
                spawnGlottalGrain(p, pitchRatio, emitVoiced);
            }
            grainCountdown -= 1.0f;

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

    void spawnGlottalGrain(const HybridParams& p, float pitchRatio, bool emitVoiced) {
        const int64_t centerPos = sampleClock - readLookback;
        if (sampleClock < readLookback + fftSize) {
            grainCountdown += 32.0f;
            wasVoicedGrain = false;
            return;
        }

        if (!emitVoiced) {
            // Unvoiced / bypass: bit-exact unity-overlap Hann window reconstruction (L=64, spacing=32, gain=0.5)
            constexpr int kUnvL = 64;
            for (int d = -kUnvL + 1; d < kUnvL; ++d) {
                const float w = 0.5f * (1.0f + std::cos((float) M_PI * (float) d / (float) kUnvL)) * 0.5f;
                const int rIdx = (int) ((centerPos + d) & histMask);
                const int wIdx = (int) ((sampleClock + d) & histMask);
                for (int c = 0; c < ch; ++c) resRegenRing[(size_t) c][(size_t) wIdx] += resHist[(size_t) c][(size_t) rIdx] * w;
            }
            grainCountdown += 32.0f;
            wasVoicedGrain = false;
            return;
        }

        // Voiced: locate single-cycle smoothed GCI peak on <650 Hz LP residual (zero harmonic double-picking)
        const float tIn = (stage2F0 > 45.0f) ? std::clamp((float) sr / stage2F0, 28.0f, (float) maxHalfWidth) : (float) (sr / 150.0);
        const float agePos = std::clamp(p.age / 50.0f, 0.0f, 1.0f);
        const float agePitchSt = agePos > 0.02f ? agePos * agePos * (0.20f * (float) std::sin(2.0 * M_PI * tremorPhase) + 0.12f * nextNoise()) : 0.0f;
        const float tOut = std::clamp((tIn / std::max(0.25f, pitchRatio)) * std::pow(2.0f, -agePitchSt / 12.0f), 24.0f, (float) maxHalfWidth);

        const int searchWin = std::max(8, (int) std::lround(0.45f * tIn));
        const int64_t maxSafePos = sampleClock - maxHalfWidth - 4;
        const int64_t minSafePos = sampleClock - histSize + maxHalfWidth + 8;

        // Track dominant smooth GCI polarity so consecutive epochs never flip sign
        float maxPos = 0.0f, maxNeg = 0.0f;
        for (int s = -searchWin; s <= searchWin; ++s) {
            const int64_t pos = std::clamp(centerPos + s, minSafePos, maxSafePos);
            const float v = resLpHist[(size_t) (pos & histMask)];
            if (v > maxPos) maxPos = v;
            if (-v > maxNeg) maxNeg = -v;
        }
        if (maxPos > maxNeg * 1.05f) gciPolarity += (1.0f - gciPolarity) * 0.25f;
        else if (maxNeg > maxPos * 1.05f) gciPolarity += (-1.0f - gciPolarity) * 0.25f;
        const float polSign = gciPolarity >= 0.0f ? 1.0f : -1.0f;

        auto findPeakNear = [&](int64_t approx, int radius) -> int64_t {
            int64_t best = std::clamp(approx, minSafePos, maxSafePos);
            float bestScore = -1e9f;
            for (int d = -radius; d <= radius; ++d) {
                const int64_t cand = std::clamp(approx + d, minSafePos, maxSafePos);
                const float v = resLpHist[(size_t) (cand & histMask)] * polSign;
                const float penalty = 1.0f - 0.10f * std::abs((float) d / (float) std::max(1, radius));
                if (v * penalty > bestScore) {
                    bestScore = v * penalty;
                    best = cand;
                }
            }
            return best;
        };

        if (!wasVoicedGrain || std::abs((double) centerPos - trackedGci) > 1.35 * (double) tIn) {
            trackedGci = (double) findPeakNear(centerPos, searchWin);
        } else {
            const int refineRad = std::max(4, (int) std::lround(0.18f * tIn));
            while (trackedGci < (double) centerPos - 0.50 * (double) tIn) {
                trackedGci += (double) tIn;
                const int64_t pLocal = findPeakNear((int64_t) std::llround(trackedGci), refineRad);
                trackedGci += 0.35 * ((double) pLocal - trackedGci);
            }
        }

        const int64_t gciIdx = std::clamp((int64_t) std::llround(trackedGci), minSafePos, maxSafePos);
        // 0.94 * T_in window guard ensures neighboring input GCIs have strictly zero weight even during fast glides
        const int lIn = std::clamp((int) std::lround(0.94f * tIn), 24, maxHalfWidth);
        const int lLf = std::clamp((int) std::lround(0.55f * tOut), 20, maxHalfWidth);

        double sq = 0.0;
        const int rmsHalf = lIn / 2;
        for (int d = -rmsHalf; d <= rmsHalf; ++d) {
            const float v = resHist[0][(size_t) ((gciIdx + d) & histMask)];
            sq += (double) v * v;
        }
        const float grainRms = (float) std::sqrt(sq / std::max(1, 2 * rmsHalf + 1));

        ++grainEpoch;
        const float regenAmt = std::clamp(p.regen, 0.0f, 1.0f);
        const float olaScale = std::clamp(std::sqrt(tOut / std::max(tIn, tOut)), 0.55f, 1.15f);
        const float effortBoost = std::clamp(0.65f + 0.50f * p.effort, 0.45f, 1.45f);
        const float growlAmt = std::clamp(p.growl * effortBoost, 0.0f, 1.0f) * stage2Voiced * (0.35f + 0.65f * regenAmt);
        const float dipGain = (growlAmt > 0.01f) ? ((grainEpoch & 1u) ? (1.0f - 0.28f * growlAmt) : (1.0f + 0.20f * growlAmt)) : 1.0f;
        const float ageShimmer = agePos > 0.02f ? (1.0f + 0.06f * agePos * nextNoise()) : 1.0f;
        const float grainGain = olaScale * dipGain * ageShimmer;

        const float glottalEff = std::clamp(p.glottal + agePos * 0.15f, -1.0f, 1.0f);
        const float oq = 0.32f + 0.50f * (glottalEff + 1.0f) * 0.5f;
        const float f0Norm = 1.0f / std::max(24.0f, tOut);
        const float dtPhase = 1.0f / std::max(24.0f, tOut);
        // Derivative-compensated LF glottal shaping keeps spectral tilt balanced cleanly before Stage 3 tract filter
        const float lfScale = 0.16f * regenAmt * grainRms * polSign * dipGain;
        const float subScale = (growlAmt > 0.01f && (grainEpoch & 1u) == 0u) ? 0.14f * growlAmt * grainRms * polSign : 0.0f;

        // 1. Single un-stretched whitened residual pulse centered cleanly on the input GCI
        for (int d = -lIn + 1; d < lIn; ++d) {
            const float wIn = 0.5f * (1.0f + std::cos((float) M_PI * (float) d / (float) lIn)) * grainGain;
            const int rIdx = (int) ((gciIdx + d) & histMask);
            const int wIdx = (int) ((sampleClock + d) & histMask);
            for (int c = 0; c < ch; ++c) resRegenRing[(size_t) c][(size_t) wIdx] += resHist[(size_t) c][(size_t) rIdx] * wIn;
        }

        // 2. Whitened GCI-synchronized LF glottal pulse + clean open-phase HPF pink breath
        const float sylEnv = std::max(rmsFast, rmsTail * 0.72f);
        const float breathGate = sylEnv > 0.004f ? std::clamp((sylEnv - 0.004f) / 0.020f, 0.0f, 1.0f) : 0.0f;
        const float breathBase = (0.35f + 0.65f * regenAmt) * breathGate * stage2Voiced * grainRms * 1.1f;

        for (int d = -lLf + 1; d < lLf; ++d) {
            const float wLf = 0.5f * (1.0f + std::cos((float) M_PI * (float) d / (float) lLf));
            float phase01 = oq + (float) d * dtPhase;
            phase01 -= std::floor(phase01);

            float syn = 0.0f;
            if (regenAmt > 0.01f) {
                float prevPh = phase01 - dtPhase;
                if (prevPh < 0.0f) prevPh += 1.0f;
                const float lfDiff = (lfTable.sample(phase01, glottalEff, f0Norm) - lfTable.sample(prevPh, glottalEff, f0Norm)) * 0.5f;
                syn -= lfDiff * lfScale;
            }
            if (subScale != 0.0f) {
                float subPh = oq + (float) d * (0.5f * dtPhase);
                subPh -= std::floor(subPh);
                syn -= lfTable.sample(subPh, -0.85f, f0Norm * 0.5f) * subScale;
            }
            const int wIdx = (int) ((sampleClock + d) & histMask);
            if (syn != 0.0f) {
                const float vSyn = syn * wLf;
                for (int c = 0; c < ch; ++c) resRegenRing[(size_t) c][(size_t) wIdx] += vSyn;
            }
            if (breathBase > 1e-6f) {
                const float openEnv = phase01 < oq ? std::sin((float) M_PI * phase01 / std::max(0.05f, oq)) : 0.0f;
                const float bSample = breathSrcRing[(size_t) wIdx];
                breathRegenRing[(size_t) wIdx] += bSample * wLf * breathBase * (0.25f + 0.75f * openEnv * openEnv);
            }
        }

        grainCountdown += tOut;
        wasVoicedGrain = true;
    }

    void processFrame(const HybridParams& p, bool useNeural) {
        const int64_t frameEnd = sampleClock;
        const int bin1000 = std::clamp((int) std::lround(1000.0 / binHz), 1, halfBins - 1);

        // =========================================================================
        // STAGE 1: Causal Minimum-Phase Glottal Inverse Filtering (raw -> resOut)
        // =========================================================================
        const int64_t t1 = frameEnd - fftSize / 2;
        const auto ye1 = yinTimeline.query(t1);
        const auto ne1 = neuralTimeline.query(t1);
        const float f0_1 = ye1.f0 > 42.0f ? ye1.f0 : (useNeural && ne1.f0 > 42.0f ? ne1.f0 : 0.0f);

        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((frameEnd - fftSize + m) & histMask);
            float s = 0.0f;
            for (int c = 0; c < ch; ++c) s += dryInRing[(size_t) c][(size_t) rIdx];
            s /= (float) ch;
            fftWork[(size_t) m] = {s * hannWin[(size_t) m], 0.0f};
        }
        fft(fftWork, false);
        for (int k = 0; k < halfBins; ++k) magDryMono1[(size_t) k] = std::abs(fftWork[(size_t) k]);

        warper.extractEnvelope(magDryMono1.data(), f0_1, envDry1.data(), nullptr, tiltDry1.data());
        const float level1k = std::max(1e-4f, tiltDry1[(size_t) bin1000]);
        for (int k = 0; k < halfBins; ++k) invNorm1[(size_t) k] = std::clamp(envDry1[(size_t) k] / level1k, 0.08f, 12.0f);
        warper.computeMinimumPhaseFilter(invNorm1.data(), inTractMp.data());

        for (int c = 0; c < ch; ++c) {
            for (int m = 0; m < fftSize; ++m) {
                const int rIdx = (int) ((frameEnd - fftSize + m) & histMask);
                dryChanWork[(size_t) m] = {dryInRing[(size_t) c][(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
            }
            fft(dryChanWork, false);
            for (int k = 0; k < halfBins; ++k) {
                const std::complex<float> d = dryChanWork[(size_t) k];
                const std::complex<float> h = inTractMp[(size_t) k];
                const float mag2 = std::norm(h);
                const std::complex<float> res = mag2 > 1e-8f ? (d * std::conj(h)) / mag2 : d;
                resWork[(size_t) k] = res;
                if (k > 0 && k < fftSize / 2) resWork[(size_t) (fftSize - k)] = std::conj(res);
            }
            fft(resWork, true);
            for (int m = 0; m < fftSize; ++m) {
                const int wIdx = (int) ((frameEnd + m) & histMask);
                float v = resWork[(size_t) m].real() * synthWin[(size_t) m];
                if (!std::isfinite(v)) v = 0.0f;
                resOutRing[(size_t) c][(size_t) wIdx] += v;
            }
        }

        // =========================================================================
        // STAGE 2 TRACKING UPDATE: Sample-aligned pitch & voicing at readLookback
        // =========================================================================
        const int64_t t2 = frameEnd - fftSize - readLookback;
        const auto ye2 = yinTimeline.query(t2);
        const auto ne2 = neuralTimeline.query(t2);
        float f0_2 = ye2.f0 > 42.0f ? ye2.f0 : (useNeural && ne2.f0 > 42.0f ? ne2.f0 : 0.0f);
        if (useNeural && ye2.f0 > 42.0f && ne2.f0 > 42.0f && std::abs(ne2.time - t2) < (int64_t) (sr * 0.22)) {
            const float ratio = ne2.f0 / ye2.f0;
            if (std::abs(ratio - 0.5f) < 0.09f || std::abs(ratio - 2.0f) < 0.16f) f0_2 = ne2.f0;
        }
        const float conf2 = ye2.f0 > 42.0f ? ye2.confidence : (useNeural ? ne2.confidence : ye2.confidence);
        const float aper2 = std::min(ye2.aperiodicity, ne2.aperiodicity);
        const float rawV2 = (ye2.rms < 0.0012f && ne2.rms < 0.0012f)
                                ? 0.0f
                                : std::clamp((conf2 - 0.24f) / 0.35f, 0.0f, 1.0f) * std::clamp(1.15f - 0.75f * aper2, 0.0f, 1.0f);
        // Track instantaneous Stage 2 pitch at t2 (zero EMA lag so single-pulse windows stay tight during glides)
        if (f0_2 > 42.0f && rawV2 > 0.22f) stage2F0 = f0_2;
        stage2Voiced += (rawV2 - stage2Voiced) * (rawV2 > stage2Voiced ? 0.60f : 0.22f);

        // =========================================================================
        // STAGE 3: Causal Minimum-Phase Target Vocal Tract Filtering
        // =========================================================================
        const int64_t dry3End = frameEnd - stage3DryDelay;
        const int64_t t3 = dry3End - fftSize / 2;
        const auto ye3 = yinTimeline.query(t3);
        const auto ne3 = neuralTimeline.query(t3);
        float f0_3 = ye3.f0 > 42.0f ? ye3.f0 : (useNeural && ne3.f0 > 42.0f ? ne3.f0 : 0.0f);
        if (useNeural && ye3.f0 > 42.0f && ne3.f0 > 42.0f && std::abs(ne3.time - t3) < (int64_t) (sr * 0.22)) {
            const float ratio = ne3.f0 / ye3.f0;
            if (std::abs(ratio - 0.5f) < 0.09f || std::abs(ratio - 2.0f) < 0.16f) f0_3 = ne3.f0;
        }
        const float conf3 = ye3.f0 > 42.0f ? ye3.confidence : (useNeural ? ne3.confidence : ye3.confidence);
        const float aper3 = std::min(ye3.aperiodicity, ne3.aperiodicity);

        double dry3Sq = 0.0;
        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((dry3End - fftSize + m) & histMask);
            float s = 0.0f;
            for (int c = 0; c < ch; ++c) s += dryInRing[(size_t) c][(size_t) rIdx];
            s /= (float) ch;
            dry3Sq += (double) s * s;
            fftWork[(size_t) m] = {s * hannWin[(size_t) m], 0.0f};
        }
        const float rms3 = (float) std::sqrt(dry3Sq / fftSize);
        fft(fftWork, false);

        const int bin100 = std::max(1, (int) (100.0f / binHz));
        const int bin1200 = std::max(bin100 + 1, (int) (1200.0f / binHz));
        const int bin3500 = std::min(halfBins - 1, (int) (3500.0f / binHz));
        const int bin12k = std::min(halfBins - 1, (int) (12000.0f / binHz));
        double eLow = 0.0, eHigh = 0.0;
        for (int k = 0; k < halfBins; ++k) {
            const float m = std::abs(fftWork[(size_t) k]);
            magDryMono3[(size_t) k] = m;
            if (k >= bin100 && k <= bin1200) eLow += (double) m * m;
            if (k >= bin3500 && k <= bin12k) eHigh += (double) m * m;
        }
        const float hfRatio3 = (float) (eHigh / (eLow + 1e-9));
        const float rawV3 = rms3 < 0.0012f
                                ? 0.0f
                                : std::clamp((conf3 - 0.24f) / 0.35f, 0.0f, 1.0f) * std::clamp(1.15f - 0.75f * aper3, 0.0f, 1.0f);
        const float tgtVoiced3 = rawV3 * (hfRatio3 > 1.8f ? 1.0f - std::clamp((hfRatio3 - 1.8f) / 2.2f, 0.0f, 0.85f) : 1.0f);
        stage3Voiced += (tgtVoiced3 - stage3Voiced) * (tgtVoiced3 > stage3Voiced ? 0.50f : 0.20f);
        if (f0_3 > 42.0f && rawV3 > 0.22f) stage3F0 += (f0_3 - stage3F0) * 0.50f;

        const float rawSib = (p.consonantGuard && rms3 > 0.0015f && hfRatio3 > 0.45f && rawV3 < 0.72f)
                                 ? std::clamp((hfRatio3 - 0.45f) / 0.85f, 0.0f, 1.0f) * std::clamp((0.72f - rawV3) / 0.45f, 0.0f, 1.0f)
                                 : 0.0f;
        smoothSibilant += (rawSib - smoothSibilant) * (rawSib > smoothSibilant ? 0.75f : 0.35f);

        // Extract pure formant tract & broadband slope from Stage-3 aligned dry frame
        warper.extractEnvelope(magDryMono3.data(), f0_3, envDry3.data(), pureTract3.data(), tiltDry3.data());
        timbre.learnSpeaker(envDry3.data(), stage3Voiced);

        const float vFactor = std::clamp((stage3Voiced - 0.12f) / 0.40f, 0.0f, 1.0f);
        const float consRelax = p.consonantGuard ? (0.25f + 0.75f * vFactor) : 1.0f;
        const float effFormant = p.formantSemitones * consRelax;
        const float activeMix = p.characterIndex > 0
                                    ? 1.0f
                                    : std::clamp(p.regen + std::abs(p.formantSemitones) / 4.0f + std::abs(p.pitchSemitones) / 8.0f +
                                                     std::abs(p.glottal) + p.growl,
                                                 0.0f, 1.0f);
        const float regenAmt = std::clamp(p.regen, 0.0f, 1.0f);
        const float sharpness = activeMix * vFactor * (0.25f + 0.35f * regenAmt);
        warper.warpEnvelope(pureTract3.data(), effFormant, sharpness, warpedTract3.data());

        float hnrBias = 0.0f;
        timbre.evaluate(warpedTract3.data(), p, neuralGain3.data(), hnrBias);

        // Build target minimum-phase vocal tract magnitude filter
        const float tilt1k_3 = std::max(1e-4f, tiltDry3[(size_t) bin1000]);
        const float neuralExp = (0.35f + 0.65f * regenAmt) * vFactor;
        const float pitchRatio = std::pow(2.0f, p.pitchSemitones / 12.0f);
        const float f0Out3 = std::clamp(stage3F0 * pitchRatio, 48.0f, 560.0f);
        const float fSub = std::max(42.0f, 0.72f * f0Out3);
        const float headWeight = std::clamp(std::max(0.0f, p.glottal) * 0.75f + std::clamp((p.pitchSemitones - 1.5f) / 10.0f, 0.0f, 1.0f) * 0.45f, 0.0f, 1.0f) * vFactor;
        const float chestWeight = std::clamp(std::max(0.0f, -p.glottal) * 0.75f + std::clamp((-p.pitchSemitones - 1.5f) / 10.0f, 0.0f, 1.0f) * 0.45f, 0.0f, 1.0f) * vFactor;

        for (int k = 0; k < halfBins; ++k) {
            const float f = std::max(20.0f, (float) k * binHz);
            const float tiltNorm = std::clamp(tiltDry3[(size_t) k] / tilt1k_3, 0.08f, 8.0f);
            const float neuralTerm = std::pow(std::clamp(neuralGain3[(size_t) k], 0.25f, 3.8f), neuralExp);

            float glotDb = 0.0f;
            if (headWeight > 0.005f) {
                const float octF0 = std::log2(std::max(30.0f, f) / f0Out3);
                glotDb += regenAmt * headWeight * 3.0f * std::exp(-octF0 * octF0 / 0.22f);
                const float octGlare = std::log2(std::max(30.0f, f) / 2900.0f);
                glotDb -= regenAmt * headWeight * 2.0f * std::exp(-octGlare * octGlare / 0.45f);
            }
            if (chestWeight > 0.005f) {
                const float octChest = std::log2(std::max(30.0f, f) / 130.0f);
                glotDb += regenAmt * chestWeight * 2.6f * std::exp(-octChest * octChest / 0.55f);
                const float octBox = std::log2(std::max(30.0f, f) / 450.0f);
                glotDb -= regenAmt * chestWeight * 1.4f * std::exp(-octBox * octBox / 0.30f);
                const float octPres = std::log2(std::max(30.0f, f) / 4200.0f);
                glotDb += regenAmt * chestWeight * 2.2f * std::exp(-octPres * octPres / 0.75f);
            }
            if (f < fSub && vFactor > 0.1f) glotDb -= 8.0f * vFactor * (1.0f - f / fSub);
            const float glotTerm = std::pow(10.0f, glotDb / 20.0f);

            targetTractMag[(size_t) k] = std::clamp(warpedTract3[(size_t) k] * tiltNorm * neuralTerm * glotTerm, 0.06f, 16.0f);
        }

        warper.computeMinimumPhaseFilter(targetTractMag.data(), targetTractMp.data());

        // Prepare open-phase breath FFT synchronized to Stage 3 window
        const int64_t regEnd = frameEnd - writeMargin;
        for (int m = 0; m < fftSize; ++m) {
            const int rIdx = (int) ((regEnd - fftSize + m) & histMask);
            breathWork[(size_t) m] = {breathRegenRing[(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
        }
        fft(breathWork, false);

        const float breathMix = std::clamp(p.breath + std::max(0.0f, hnrBias) * 0.10f, 0.0f, 1.0f) * vFactor;
        const int bin4500 = std::min(halfBins - 1, (int) (4500.0f / binHz));

        for (int c = 0; c < ch; ++c) {
            for (int m = 0; m < fftSize; ++m) {
                const int rIdx = (int) ((regEnd - fftSize + m) & histMask);
                regenChanWork[(size_t) m] = {resRegenRing[(size_t) c][(size_t) rIdx] * hannWin[(size_t) m], 0.0f};
            }
            fft(regenChanWork, false);

            double outBandPow = 0.0;
            double dryBandPow = 0.0;
            for (int k = 0; k < halfBins; ++k) {
                const std::complex<float> exc = regenChanWork[(size_t) k] + breathWork[(size_t) k] * (breathMix * 0.45f);
                const std::complex<float> yOut = exc * targetTractMp[(size_t) k];
                outChanWork[(size_t) k] = yOut;
                if (k > 0 && k < fftSize / 2) outChanWork[(size_t) (fftSize - k)] = std::conj(yOut);
                if (k >= 1 && k <= bin4500) {
                    outBandPow += (double) std::norm(yOut);
                    dryBandPow += (double) magDryMono3[(size_t) k] * magDryMono3[(size_t) k];
                }
            }

            // Gentle soft frame-level balance against input energy (never spikes >1.85x or drops <0.50x)
            if (outBandPow > 1e-8 && dryBandPow > 1e-8) {
                const float gainFix = std::clamp((float) std::sqrt(std::sqrt(dryBandPow / outBandPow)), 0.55f, 1.75f);
                for (int k = 0; k < fftSize; ++k) outChanWork[(size_t) k] *= gainFix;
            }

            fft(outChanWork, true);
            for (int m = 0; m < fftSize; ++m) {
                const int wIdx = (int) ((frameEnd + m) & histMask);
                float v = outChanWork[(size_t) m].real() * synthWin[(size_t) m];
                if (!std::isfinite(v)) v = 0.0f;
                outRing[(size_t) c][(size_t) wIdx] += v;
            }
        }

        status.voiced = stage3Voiced;
        status.consonant = smoothSibilant;
        status.regen = regenAmt * stage3Voiced;
        status.subharmonic = std::clamp(p.growl * stage3Voiced, 0.0f, 1.0f);
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
    int ch = 2, fftSize = 1024, halfBins = 513, hop = 128;
    float binHz = 46.875f;
    int readLookback = 768, writeMargin = 512, maxHalfWidth = 496;
    int stage3DryDelay = 2304, totalLatency = 3328;
    int histSize = 8192, histMask = 8191;

    VocalTractWarper warper;
    LFGlottalTable lfTable;
    NeuralTimbreShaper timbre;
    PitchTimeline neuralTimeline, yinTimeline;

    std::vector<float> hannWin, synthWin;
    std::vector<std::complex<float>> twFwd, twInv;
    std::vector<std::vector<float>> dryInRing, resOutRing, resHist, resRegenRing, outRing;
    std::vector<float> resLpHist, breathSrcRing, breathRegenRing;

    std::vector<float> magDryMono1, envDry1, tiltDry1, invNorm1;
    std::vector<float> magDryMono3, envDry3, pureTract3, tiltDry3, warpedTract3, neuralGain3, targetTractMag;
    std::vector<std::complex<float>> fftWork, dryChanWork, resWork, regenChanWork, breathWork, outChanWork;
    std::vector<std::complex<float>> inTractMp, targetTractMp;

    int64_t sampleClock = 0;
    int hopCounter = 0;
    double tremorPhase = 0.0;
    uint32_t rngState = 0x9E3779B9u;

    float grainCountdown = 0.0f;
    double trackedGci = 0.0;
    bool wasVoicedGrain = false;
    float gciPolarity = 1.0f;
    uint32_t grainEpoch = 0;

    float resLpIc1 = 0.0f, resLpIc2 = 0.0f;
    float breathPink = 0.0f, breathHpIc1 = 0.0f, breathHpIc2 = 0.0f;
    float rmsFast = 0.0f, rmsTail = 0.0f;

    float stage2F0 = 140.0f, stage2Voiced = 0.0f;
    float stage3F0 = 140.0f, stage3Voiced = 0.0f;
    float smoothSibilant = 0.0f;
    HybridStatus status{};
};

} // namespace adub
