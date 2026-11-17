// AlptugDUB by Alptuğ Dağ — human-voice-optimized adaptive Auto-Tune with automatic key & scale detection.
#pragma once

#include "VoiceProfile.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace adub {

/**
 * Adaptive vocal pitch corrector:
 *  - Learns the speaker/singer's musical key and scale (Major / Minor / Chromatic) automatically
 *    via Krumhansl-Schmuckler chroma correlation
 *  - Separates note center from natural 4.5-7.5 Hz human vibrato, preserving vibrato at moderate
 *    settings and tightening toward modern studio lock at high settings
 *  - Adapts retune speed to the speaker's learned VoiceType (deeper voices tracked gently)
 */
class AutoTune {
public:
    enum class ScaleType { Major = 0, Minor = 1, Chromatic = 2 };

    struct Result {
        float correctionSemitones = 0.0f; // delta pitch (st) to add on top of Pitch knob
        int key = 0;                      // 0=C .. 11=B (in output key)
        ScaleType scale = ScaleType::Chromatic;
        float targetMidi = 60.0f;
    };

    void prepare(double sampleRate) {
        sr = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() {
        chroma.fill(0.0f);
        totalChroma = 0.0f;
        detectedInKey = 0;
        detectedScale = ScaleType::Chromatic;
        centerMidi = 0.0f;
        prevMidi = 0.0f;
        lockedTargetMidi = 60.0f;
        hasTarget = false;
        wasVoiced = false;
        smoothDelta = 0.0f;
        lastResult = Result{};
    }

    void resetProfile() {
        chroma.fill(0.0f);
        totalChroma = 0.0f;
        detectedInKey = 0;
        detectedScale = ScaleType::Chromatic;
    }

    /**
     * Updates Auto-Tune for a block/hop of `dtSec` seconds.
     *  - `f0In`: current input pitch (Hz), <= 0 when unvoiced
     *  - `confidence`: 0..1 voicing probability
     *  - `pitchShiftSt`: user's Pitch knob + age/dynamics (semitones)
     *  - `amount`: 0..1 Auto-Tune strength
     *  - `voiceType`: learned speaker voice type (optimizes retune speed)
     */
    Result update(float f0In, float confidence, float pitchShiftSt, float amount, VoiceType voiceType, float dtSec) {
        const bool voiced = f0In > 45.0f && f0In < 1050.0f && confidence >= 0.42f;
        const float dt = std::clamp(dtSec, 0.0005f, 0.05f);

        if (voiced) {
            const float midiIn = 69.0f + 12.0f * std::log2(f0In / 440.0f);

            // 1. Update Krumhansl-Schmuckler 12-bin chroma histogram on stable voiced frames
            const float dMidi = wasVoiced ? std::abs(midiIn - prevMidi) : 0.0f;
            if (confidence > 0.55f && dMidi < 0.75f) {
                const float keep = totalChroma > 12.0f ? std::exp(-dt / 25.0f) : 1.0f;
                for (float& c : chroma) c *= keep;
                totalChroma *= keep;

                const float pcFloat = std::fmod(midiIn + 1200.0f, 12.0f);
                const int pc0 = ((int) std::floor(pcFloat)) % 12;
                const int pc1 = (pc0 + 1) % 12;
                const float frac = pcFloat - std::floor(pcFloat);
                const float w = confidence * dt * 20.0f;
                // Sharpen chroma weight for pitches close to semitone centers
                const float tuneWeight = 1.0f - 0.5f * std::sin((float) M_PI * frac);
                chroma[(size_t) pc0] += w * (1.0f - frac) * tuneWeight;
                chroma[(size_t) pc1] += w * frac * tuneWeight;
                totalChroma += w * tuneWeight;

                if (totalChroma >= 4.0f) detectKeyAndScale();
            }

            // 2. Separate slow note center (~90 ms) from natural human vibrato
            if (!wasVoiced || std::abs(midiIn - centerMidi) > 2.5f) {
                centerMidi = midiIn;
                hasTarget = false;
            } else {
                const float kCenter = 1.0f - std::exp(-dt / 0.090f);
                centerMidi += (midiIn - centerMidi) * kCenter;
            }
            const float vibratoMidi = midiIn - centerMidi;
            prevMidi = midiIn;

            // 3. Determine output musical key and snap shifted center with 0.18 st hysteresis
            const int shiftInt = (int) std::lround(pitchShiftSt);
            const int outKey = ((detectedInKey + shiftInt) % 12 + 12) % 12;
            const float shiftedCenter = centerMidi + pitchShiftSt;
            const float candTarget = nearestScaleMidi(shiftedCenter, outKey, detectedScale);

            if (!hasTarget) {
                lockedTargetMidi = candTarget;
                hasTarget = true;
            } else if (std::abs(candTarget - lockedTargetMidi) > 0.5f) {
                const float distOld = std::abs(shiftedCenter - lockedTargetMidi);
                const float distNew = std::abs(shiftedCenter - candTarget);
                if (distNew + 0.18f < distOld) lockedTargetMidi = candTarget;
            }

            // 4. Human-voice optimized retune physics
            if (amount <= 0.005f) {
                smoothDelta *= std::exp(-dt / 0.025f);
            } else {
                const float baseTau = (voiceType == VoiceType::Bass || voiceType == VoiceType::Baritone) ? 0.022f : 0.012f;
                const float speedFactor = 0.45f + 1.8f * (1.0f - amount);
                const float tau = std::clamp(baseTau * speedFactor, 0.005f, 0.070f);
                const float kRetune = 1.0f - std::exp(-dt / tau);

                // Keep 85-100% of natural vibrato for amount <= 0.65, then tighten toward modern lock
                const float vibratoKeep = amount <= 0.65f ? (1.0f - 0.15f * (amount / 0.65f))
                                                          : 0.85f * std::max(0.0f, 1.0f - (amount - 0.65f) / 0.35f);

                const float desiredMidi = lockedTargetMidi + vibratoMidi * vibratoKeep;
                const float rawCorr = desiredMidi - (midiIn + pitchShiftSt);
                const float voicedGain = std::clamp((confidence - 0.40f) / 0.25f, 0.0f, 1.0f);
                const float targetCorr = std::clamp(rawCorr * amount * voicedGain, -4.0f, 4.0f);
                smoothDelta += (targetCorr - smoothDelta) * kRetune;
            }

            lastResult.key = outKey;
            lastResult.scale = totalChroma >= 4.0f ? detectedScale : ScaleType::Chromatic;
            lastResult.targetMidi = lockedTargetMidi;
            lastResult.correctionSemitones = smoothDelta;
        } else {
            // Smoothly relax correction during unvoiced consonants & silences
            smoothDelta *= std::exp(-dt / 0.025f);
            hasTarget = false;
            lastResult.correctionSemitones = smoothDelta;
        }

        wasVoiced = voiced;
        return lastResult;
    }

    const Result& latest() const { return lastResult; }

private:
    void detectKeyAndScale() {
        // Krumhansl-Schmuckler perceptual tonal hierarchy profiles
        static constexpr std::array<float, 12> kMajor = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
                                                         2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
        static constexpr std::array<float, 12> kMinor = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
                                                         2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

        double cMean = 0.0;
        for (float v : chroma) cMean += v;
        cMean /= 12.0;

        auto corrWith = [&](int root, const std::array<float, 12>& prof) {
            double pMean = 0.0;
            for (float v : prof) pMean += v;
            pMean /= 12.0;
            double num = 0.0, dc = 0.0, dp = 0.0;
            for (int i = 0; i < 12; ++i) {
                const double c = chroma[(size_t) ((root + i) % 12)] - cMean;
                const double p = prof[(size_t) i] - pMean;
                num += c * p;
                dc += c * c;
                dp += p * p;
            }
            const double den = std::sqrt(dc * dp);
            return den > 1e-9 ? (float) (num / den) : 0.0f;
        };

        float bestCorr = -2.0f;
        int bestKey = 0;
        ScaleType bestScale = ScaleType::Major;
        for (int k = 0; k < 12; ++k) {
            const float cmaj = corrWith(k, kMajor);
            if (cmaj > bestCorr) {
                bestCorr = cmaj;
                bestKey = k;
                bestScale = ScaleType::Major;
            }
            const float cmin = corrWith(k, kMinor);
            if (cmin > bestCorr) {
                bestCorr = cmin;
                bestKey = k;
                bestScale = ScaleType::Minor;
            }
        }
        detectedInKey = bestKey;
        detectedScale = bestCorr > 0.18f ? bestScale : ScaleType::Chromatic;
    }

    static float nearestScaleMidi(float midi, int key, ScaleType scale) {
        if (scale == ScaleType::Chromatic) return std::round(midi);
        static constexpr std::array<int, 7> majorSteps = {0, 2, 4, 5, 7, 9, 11};
        static constexpr std::array<int, 7> minorSteps = {0, 2, 3, 5, 7, 8, 10};
        const auto& steps = (scale == ScaleType::Major) ? majorSteps : minorSteps;

        const int baseOct = (int) std::floor((midi - (float) key) / 12.0f);
        float best = std::round(midi);
        float bestDist = 1e6f;
        for (int oct = baseOct - 1; oct <= baseOct + 1; ++oct) {
            for (int st : steps) {
                const float cand = (float) (oct * 12 + key + st);
                const float dist = std::abs(midi - cand);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = cand;
                }
            }
        }
        return best;
    }

    double sr = 48000.0;
    std::array<float, 12> chroma{};
    float totalChroma = 0.0f;
    int detectedInKey = 0;
    ScaleType detectedScale = ScaleType::Chromatic;
    float centerMidi = 0.0f, prevMidi = 0.0f, lockedTargetMidi = 60.0f, smoothDelta = 0.0f;
    bool hasTarget = false, wasVoiced = false;
    Result lastResult{};
};

} // namespace adub
