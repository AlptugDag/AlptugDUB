// AlptugDUB by Alptuğ Dağ — studio adaptive Auto-Tune with melisma tracking & scale lock (v3.0).
#pragma once

#include "VoiceProfile.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace adub {

/**
 * Re-engineered Studio Vocal Auto-Tune Engine (v3.0):
 *  - Melisma-aware fast onset detection breaks note-center lag immediately on >0.68 st leaps or >1.35 st moves
 *  - Independent Retune Speed (5..120 ms) and Vibrato Preservation (0..100%)
 *  - Manual Scale/Key Lock (Major, Minor, Chromatic, Hijaz, Pentatonic) alongside Krumhansl-Schmuckler Auto-Detect
 *  - Fractional pitch-shift accurate in output MIDI domain with 0.14 st anti-chatter hysteresis
 */
class AutoTune {
public:
    enum class ScaleMode { Auto = 0, Major = 1, Minor = 2, Chromatic = 3, Hijaz = 4, Pentatonic = 5 };
    enum class ScaleType { Major = 0, Minor = 1, Chromatic = 2, Hijaz = 3, Pentatonic = 4 };

    struct Settings {
        float amount = 0.0f;          // 0..1 correction strength
        float retuneSpeedMs = 22.0f;  // 5..120 ms retune time constant
        float vibratoPreserve = 0.75f;// 0..1 natural vibrato preservation
        ScaleMode scaleMode = ScaleMode::Auto;
        int manualKey = 0;            // 0=C .. 11=B (used when scaleMode != Auto)
    };

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

    /** Convenience overload preserving v2.0 call signature with v3.0 defaults. */
    Result update(float f0In, float confidence, float pitchShiftSt, float amount, VoiceType voiceType, float dtSec) {
        Settings s;
        s.amount = amount;
        return update(f0In, confidence, pitchShiftSt, s, voiceType, dtSec);
    }

    /** Full v3.0 studio update with independent Retune Speed, Vibrato Preservation, and Scale/Key lock. */
    Result update(float f0In, float confidence, float pitchShiftSt, const Settings& s, VoiceType voiceType, float dtSec) {
        const bool voiced = f0In > 45.0f && f0In < 1050.0f && confidence >= 0.42f;
        const float dt = std::clamp(dtSec, 0.0005f, 0.05f);

        if (voiced) {
            const float midiIn = 69.0f + 12.0f * std::log2(f0In / 440.0f);
            const float dMidi = wasVoiced ? std::abs(midiIn - prevMidi) : 0.0f;

            // 1. Update Krumhansl-Schmuckler 12-bin chroma histogram on stable voiced frames
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

                if (totalChroma >= 3.2f) detectKeyAndScale();
            }

            // 2. Melisma-aware note-center separation: fast jump (>0.68 st step or >1.35 st offset) resets immediately
            if (!wasVoiced || std::abs(midiIn - centerMidi) > 1.35f || dMidi > 0.68f) {
                centerMidi = midiIn;
                hasTarget = false;
            } else {
                const float kCenter = 1.0f - std::exp(-dt / 0.075f);
                centerMidi += (midiIn - centerMidi) * kCenter;
            }
            const float vibratoMidi = std::clamp(midiIn - centerMidi, -0.75f, 0.75f);
            prevMidi = midiIn;

            // 3. Determine active musical scale and key
            const bool isAuto = (s.scaleMode == ScaleMode::Auto);
            ScaleType activeScale = ScaleType::Chromatic;
            if (isAuto) {
                activeScale = totalChroma >= 3.2f ? detectedScale : ScaleType::Chromatic;
            } else {
                switch (s.scaleMode) {
                    case ScaleMode::Major: activeScale = ScaleType::Major; break;
                    case ScaleMode::Minor: activeScale = ScaleType::Minor; break;
                    case ScaleMode::Hijaz: activeScale = ScaleType::Hijaz; break;
                    case ScaleMode::Pentatonic: activeScale = ScaleType::Pentatonic; break;
                    case ScaleMode::Chromatic:
                    case ScaleMode::Auto:
                    default: activeScale = ScaleType::Chromatic; break;
                }
            }

            const int shiftInt = (int) std::lround(pitchShiftSt);
            const int outKey = isAuto ? (((detectedInKey + shiftInt) % 12 + 12) % 12)
                                      : (((s.manualKey % 12) + 12) % 12);

            const float shiftedCenter = centerMidi + pitchShiftSt;
            const float candTarget = nearestScaleMidi(shiftedCenter, outKey, activeScale);

            if (!hasTarget) {
                lockedTargetMidi = candTarget;
                hasTarget = true;
            } else if (std::abs(candTarget - lockedTargetMidi) > 0.5f) {
                const float distOld = std::abs(shiftedCenter - lockedTargetMidi);
                const float distNew = std::abs(shiftedCenter - candTarget);
                if (distNew + 0.14f < distOld) lockedTargetMidi = candTarget;
            }

            // 4. Independent Retune Speed + Vibrato Preservation physics
            const float amount = std::clamp(s.amount, 0.0f, 1.0f);
            if (amount <= 0.005f) {
                smoothDelta *= std::exp(-dt / 0.020f);
            } else {
                const float voiceScale = (voiceType == VoiceType::Bass || voiceType == VoiceType::Baritone) ? 1.2f : 1.0f;
                const float tauSec = std::clamp((std::clamp(s.retuneSpeedMs, 5.0f, 120.0f) / 1000.0f) * voiceScale, 0.004f, 0.140f);
                const float kRetune = 1.0f - std::exp(-dt / tauSec);

                const float vibPres = std::clamp(s.vibratoPreserve, 0.0f, 1.0f);
                const float vibratoKeep = std::clamp(vibPres * (1.0f - 0.35f * std::max(0.0f, amount - 0.70f) / 0.30f), 0.0f, 1.0f);

                const float desiredMidi = lockedTargetMidi + vibratoMidi * vibratoKeep;
                const float rawCorr = desiredMidi - (midiIn + pitchShiftSt);
                const float voicedGain = std::clamp((confidence - 0.40f) / 0.25f, 0.0f, 1.0f);
                const float targetCorr = std::clamp(rawCorr * amount * voicedGain, -4.0f, 4.0f);
                smoothDelta += (targetCorr - smoothDelta) * kRetune;
            }

            lastResult.key = outKey;
            lastResult.scale = activeScale;
            lastResult.targetMidi = lockedTargetMidi;
            lastResult.correctionSemitones = smoothDelta;
        } else {
            // Smoothly relax correction during unvoiced consonants & silences
            smoothDelta *= std::exp(-dt / 0.022f);
            hasTarget = false;
            lastResult.correctionSemitones = smoothDelta;
        }

        wasVoiced = voiced;
        return lastResult;
    }

    const Result& latest() const { return lastResult; }

    static float nearestScaleMidi(float midi, int key, ScaleType scale) {
        if (scale == ScaleType::Chromatic) return std::round(midi);
        static constexpr std::array<int, 7> majorSteps = {0, 2, 4, 5, 7, 9, 11};
        static constexpr std::array<int, 7> minorSteps = {0, 2, 3, 5, 7, 8, 10};
        static constexpr std::array<int, 7> hijazSteps = {0, 1, 4, 5, 7, 8, 10};
        static constexpr std::array<int, 5> pentaSteps = {0, 2, 4, 7, 9};

        const int* steps = majorSteps.data();
        int nSteps = 7;
        switch (scale) {
            case ScaleType::Major: steps = majorSteps.data(); nSteps = 7; break;
            case ScaleType::Minor: steps = minorSteps.data(); nSteps = 7; break;
            case ScaleType::Hijaz: steps = hijazSteps.data(); nSteps = 7; break;
            case ScaleType::Pentatonic: steps = pentaSteps.data(); nSteps = 5; break;
            case ScaleType::Chromatic:
            default: return std::round(midi);
        }

        const int baseOct = (int) std::floor((midi - (float) key) / 12.0f);
        float best = std::round(midi);
        float bestDist = 1e6f;
        for (int oct = baseOct - 1; oct <= baseOct + 1; ++oct) {
            for (int i = 0; i < nSteps; ++i) {
                const float cand = (float) (oct * 12 + key + steps[i]);
                const float dist = std::abs(midi - cand);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = cand;
                }
            }
        }
        return best;
    }

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
        detectedScale = bestCorr > 0.16f ? bestScale : ScaleType::Chromatic;
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
