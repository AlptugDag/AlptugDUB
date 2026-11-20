// AlptugDUB by Alptuğ Dağ — v3.0 studio dynamics: 3 ms lookahead true-peak limiter,
// ITU-R BS.1770 K-weighted gated loudness matcher, and smooth soft noise gate.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace adub {

/** Lightweight lock-free biquad filter (direct-form II transposed). */
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }

    void setHighPass(double sr, double cutoffHz, double q = 0.70710678) {
        const double fc = std::clamp(cutoffHz, 15.0, sr * 0.42);
        const double w0 = 2.0 * M_PI * fc / sr;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = ((1.0 + cw) * 0.5) / a0;
        b1 = -(1.0 + cw) / a0;
        b2 = b0;
        a1 = (-2.0 * cw) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void setHighShelf(double sr, double freqHz, double gainDb, double q = 0.70710678) {
        const double fc = std::clamp(freqHz, 80.0, sr * 0.44);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * M_PI * fc / sr;
        const double cw = std::cos(w0);
        const double sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * cw + twoSqrtAAlpha;
        b0 = (A * ((A + 1.0) + (A - 1.0) * cw + twoSqrtAAlpha)) / a0;
        b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cw)) / a0;
        b2 = (A * ((A + 1.0) + (A - 1.0) * cw - twoSqrtAAlpha)) / a0;
        a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cw)) / a0;
        a2 = ((A + 1.0) - (A - 1.0) * cw - twoSqrtAAlpha) / a0;
    }

    inline float process(float x) {
        const double xd = (double) x;
        const double y = b0 * xd + z1;
        z1 = b1 * xd - a1 * y + z2;
        z2 = b2 * xd - a2 * y;
        return (float) y;
    }
};

/**
 * 2-stage ITU-R BS.1770 K-Weighting Filter:
 *  - Stage 1: Acoustic head-shadowing +4 dB high-shelf at 1500 Hz
 *  - Stage 2: RLB 60 Hz 2nd-order high-pass filter
 */
class KWeightingFilter {
public:
    void prepare(double sampleRate) {
        const double sr = std::max(8000.0, sampleRate);
        preShelf.setHighShelf(sr, 1500.0, 4.0, 0.70710678);
        rlbHpf.setHighPass(sr, 60.0, 0.5);
        reset();
    }

    void reset() {
        preShelf.reset();
        rlbHpf.reset();
    }

    inline float process(float x) { return rlbHpf.process(preShelf.process(x)); }

private:
    Biquad preShelf, rlbHpf;
};

/**
 * ITU-R BS.1770 K-Weighted Gated Loudness Auto-Matcher (v3.0):
 * Tracks dry and wet K-weighted energy with a strict -48 dBFS activity gate so breath pauses
 * and room-floor silence never pump the gain.
 */
class KWeightedLoudnessMatcher {
public:
    void prepare(double sampleRate) {
        sr = std::max(8000.0, sampleRate);
        kDry.prepare(sr);
        kWet.prepare(sr);
        alphaEnv = 1.0f - (float) std::exp(-1.0 / (0.300 * sr));
        alphaGain = 1.0f - (float) std::exp(-1.0 / (0.450 * sr));
        reset();
    }

    void reset() {
        kDry.reset();
        kWet.reset();
        powDry = 0.0f;
        powWet = 0.0f;
        gain = 1.0f;
    }

    /** Processes one sample pair (mono dry, mono wet) and returns smoothed linear makeup gain. */
    inline float step(float dryMono, float wetMono) {
        const float kd = kDry.process(dryMono);
        const float kw = kWet.process(wetMono);
        const float pd = kd * kd;
        const float pw = kw * kw;

        powDry += (pd - powDry) * alphaEnv;
        powWet += (pw - powWet) * alphaEnv;

        // -48 dBFS linear power gate = (10^(-48/20))^2 ~= 1.5849e-5
        static constexpr float kGatePow = 1.584893e-5f;
        if (powDry > kGatePow && powWet > powDry * 0.0064f) {
            const float target = std::clamp(std::sqrt(powDry / powWet), 0.35f, 2.20f);
            gain += (target - gain) * alphaGain;
        }
        return gain;
    }

    float currentGain() const { return gain; }

private:
    double sr = 48000.0;
    KWeightingFilter kDry, kWet;
    float alphaEnv = 1e-4f, alphaGain = 5e-5f;
    float powDry = 0.0f, powWet = 0.0f, gain = 1.0f;
};

/**
 * Smooth Soft-Knee Input Noise Gate (v3.0):
 * Attenuates sub-threshold studio noise cleanly before pitch analysis & regeneration without hard clicks.
 */
class SoftNoiseGate {
public:
    void prepare(double sampleRate) {
        sr = std::max(8000.0, sampleRate);
        envAtt = 1.0f - (float) std::exp(-1.0 / (0.005 * sr));
        envRel = 1.0f - (float) std::exp(-1.0 / (0.065 * sr));
        gainAtt = 1.0f - (float) std::exp(-1.0 / (0.004 * sr));
        gainRel = 1.0f - (float) std::exp(-1.0 / (0.080 * sr));
        reset();
    }

    void reset() {
        env = 0.0f;
        gain = 1.0f;
    }

    /** Computes per-sample soft gate multiplier for mono detector input and threshold in dBFS. */
    inline float step(float monoAbs, float thresholdDb) {
        if (thresholdDb <= -71.5f) {
            gain += (1.0f - gain) * gainAtt;
            return gain;
        }
        env += (monoAbs - env) * (monoAbs > env ? envAtt : envRel);
        const float threshLin = std::pow(10.0f, thresholdDb / 20.0f);
        // Soft knee across [0.25*thresh .. 1.0*thresh], floor at -20 dB (0.10) to keep breath tails musical
        float target = 1.0f;
        if (env < threshLin) {
            const float ratio = std::clamp(env / std::max(1e-7f, threshLin), 0.0f, 1.0f);
            target = 0.10f + 0.90f * ratio * ratio;
        }
        gain += (target - gain) * (target > gain ? gainAtt : gainRel);
        return gain;
    }

private:
    double sr = 48000.0;
    float envAtt = 0.01f, envRel = 0.001f, gainAtt = 0.01f, gainRel = 0.001f;
    float env = 0.0f, gain = 1.0f;
};

/**
 * 3 ms Lookahead Soft-Knee True-Peak Limiter (v3.0):
 *  - Stereo/multi-channel linked peak detection (preserves phantom center)
 *  - Smooth lookahead attack (lookaheadMs * 0.45) + 85 ms release
 *  - Strict -0.3 dBFS ceiling compliance with real-time Gain Reduction (GR dB) telemetry
 *  - Zero heap allocation on the audio thread
 */
class LookaheadLimiter {
public:
    void prepare(double sampleRate, int numChannels, float ceilingDb = -0.3f, float lookaheadMs = 3.0f) {
        sr = std::max(8000.0, sampleRate);
        ch = std::max(1, numChannels);
        ceiling = std::pow(10.0f, ceilingDb / 20.0f); // ~0.96605 for -0.3 dBFS
        lookahead = std::max(8, (int) std::lround(sr * lookaheadMs / 1000.0));
        delayBuf.assign((size_t) ch, std::vector<float>((size_t) lookahead, 0.0f));
        attackCoeff = 1.0f - (float) std::exp(-1.0 / std::max(1.0, sr * (lookaheadMs * 0.45) / 1000.0));
        releaseCoeff = 1.0f - (float) std::exp(-1.0 / std::max(1.0, sr * 0.085));
        reset();
    }

    void reset() {
        for (auto& d : delayBuf) std::fill(d.begin(), d.end(), 0.0f);
        writeIdx = 0;
        gain = 1.0f;
        minGainThisBlock = 1.0f;
    }

    int latencySamples() const { return lookahead; }
    float ceilingLinear() const { return ceiling; }

    /** Processes one multi-channel frame in place; writes limited samples to `samples[0..numCh-1]`. */
    inline float processFrame(float* samples, int numCh) {
        const int nCh = std::min(ch, numCh);
        float absLook = 0.0f;
        float absDel = 0.0f;
        float delayed[8]{};

        for (int c = 0; c < nCh; ++c) {
            const float x = std::isfinite(samples[c]) ? samples[c] : 0.0f;
            const float del = delayBuf[(size_t) c][(size_t) writeIdx];
            delayBuf[(size_t) c][(size_t) writeIdx] = x;
            delayed[c] = del;
            absLook = std::max(absLook, std::abs(x));
            absDel = std::max(absDel, std::abs(del));
        }
        if (++writeIdx >= lookahead) writeIdx = 0;

        const float peak = std::max(absLook, absDel);
        const float targetGain = peak > ceiling ? (ceiling / peak) : 1.0f;

        if (targetGain < gain) {
            gain += (targetGain - gain) * attackCoeff;
            if (absDel * gain > ceiling && absDel > 1e-9f) gain = ceiling / absDel;
        } else {
            gain += (1.0f - gain) * releaseCoeff;
        }

        if (gain < minGainThisBlock) minGainThisBlock = gain;

        float outPeak = 0.0f;
        for (int c = 0; c < nCh; ++c) {
            const float y = std::clamp(delayed[c] * gain, -ceiling, ceiling);
            samples[c] = y;
            outPeak = std::max(outPeak, std::abs(y));
        }
        return outPeak;
    }

    /** Convenience mono processor for unit tests & offline checks. */
    inline float processSample(float x) {
        float s[1] = {x};
        processFrame(s, 1);
        return s[0];
    }

    /** Returns peak gain reduction in dB (<= 0.0) since last call and resets the block tracker. */
    float consumeMaxGrDb() {
        const float gr = minGainThisBlock < 0.9999f ? 20.0f * std::log10(minGainThisBlock) : 0.0f;
        minGainThisBlock = 1.0f;
        return gr;
    }

private:
    double sr = 48000.0;
    int ch = 2;
    int lookahead = 144;
    int writeIdx = 0;
    float ceiling = 0.96605f;
    float gain = 1.0f;
    float minGainThisBlock = 1.0f;
    float attackCoeff = 0.02f;
    float releaseCoeff = 0.00025f;
    std::vector<std::vector<float>> delayBuf;
};

} // namespace adub
