// AlptugDUB by Alptuğ Dağ — voice pitch tracking (v3.0).
//
// YIN (de Cheveigné & Kawahara, 2002) with 2nd-order Butterworth HPF rumble rejection,
// anti-aliased decimation to ~12 kHz, and octave-continuity tracking across speech and singing.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace adub {

/** One estimate: the period at a point in time, or unvoiced. */
struct PitchEstimate {
    int64_t time = 0;        // input sample this estimate describes (its window centre)
    float period = 0.0f;     // in input samples; 0 when unvoiced
    float aperiodicity = 1;  // YIN's d'(tau): 0 = perfectly periodic, 1 = noise
    float rms = 0.0f;
    float confidence = 0.0f; // probability that the frame is voiced
    float candidate = 0.0f;  // best period even when unvoiced
    bool voiced() const { return period > 0.0f; }
};

class PitchTracker {
public:
    static constexpr float kMinF0 = 50.0f;
    static constexpr float kMaxF0 = 820.0f;

    void prepare(double sampleRate, double hpfCutoffHz = 75.0) {
        sr = std::max(8000.0, sampleRate);
        decim = std::max(1, (int) std::lround(sr / 12000.0));
        dsr = sr / decim;
        window = (int) std::lround(dsr * 0.028); // integration window: 28 ms
        maxLag = (int) std::ceil(dsr / kMinF0);
        minLag = std::max(2, (int) std::floor(dsr / kMaxF0));
        ring.assign((size_t) (nextPow2(window + maxLag + 8) * 2), 0.0f);
        mask = (int) ring.size() - 1;
        diff.assign((size_t) (maxLag + 2), 0.0f);
        // anti-alias: two biquads at 0.42 of the decimated Nyquist
        designLowpass(0.42 * dsr * 0.5 / sr);
        setHighPassHz(hpfCutoffHz);
        reset();
    }

    /** Configures the pre-conditioning 2nd-order Butterworth HPF (<= 20 Hz bypasses HPF). */
    void setHighPassHz(double cutoffHz) {
        if (cutoffHz <= 20.0) {
            hpfEnabled = false;
            return;
        }
        const double fc = std::clamp(cutoffHz, 25.0, sr * 0.35);
        if (hpfEnabled && std::abs(fc - hpfHz) < 0.25) return;
        hpfHz = fc;
        hpfEnabled = true;
        const double w = 2.0 * M_PI * fc / sr;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;
        hb0 = ((1.0 + cw) * 0.5) / a0;
        hb1 = -(1.0 + cw) / a0;
        hb2 = hb0;
        ha1 = (-2.0 * cw) / a0;
        ha2 = (1.0 - alpha) / a0;
    }

    void reset() {
        std::fill(ring.begin(), ring.end(), 0.0f);
        z1a = z2a = z1b = z2b = 0.0;
        hz1 = hz2 = 0.0;
        phase = 0;
        written = 0;
        inCount = 0;
        prevF0 = 0.0f;
        unvoicedStreak = 0;
    }

    /** Feeds one input sample (mono). */
    void push(float x) {
        double y = (double) x;
        if (hpfEnabled) {
            const double out = hb0 * y + hz1;
            hz1 = hb1 * y - ha1 * out + hz2;
            hz2 = hb2 * y - ha2 * out;
            y = out;
        }
        y = biquad((float) y, z1a, z2a);
        y = biquad((float) y, z1b, z2b);
        ++inCount;
        if (++phase >= decim) {
            phase = 0;
            ring[(size_t) (written & mask)] = (float) y;
            ++written;
        }
    }

    /** How far behind the newest input sample an estimate's centre lies. */
    int64_t delaySamples() const { return (int64_t) ((window + maxLag) * 0.5 * decim); }

    /** Analyses the newest window with octave-continuity protection. */
    PitchEstimate estimate() {
        PitchEstimate e;
        e.time = inCount - delaySamples();
        const int64_t start = written - (window + maxLag);
        if (start < 0) return e;
        double energy = 0;
        for (int i = 0; i < window; ++i) {
            float v = at(start + i);
            energy += v * v;
        }
        e.rms = (float) std::sqrt(energy / window);
        if (e.rms < 1.2e-4f) {
            if (++unvoicedStreak > 40) prevF0 = 0.0f;
            return e; // about -78 dBFS: silence
        }

        // difference function d(tau) and its cumulative-mean normalisation d'(tau)
        diff[0] = 1.0f;
        double running = 0;
        for (int tau = 1; tau <= maxLag; ++tau) {
            double d = 0;
            for (int i = 0; i < window; ++i) {
                float a = at(start + i) - at(start + i + tau);
                d += a * a;
            }
            running += d;
            diff[(size_t) tau] = running > 1e-12 ? (float) (d * tau / running) : 1.0f;
        }
        float minDiff = 1.0f;
        int minTau = minLag;
        for (int tau = minLag; tau <= maxLag; ++tau) {
            if (diff[(size_t) tau] < minDiff) {
                minDiff = diff[(size_t) tau];
                minTau = tau;
            }
        }

        const double expectedTau = (prevF0 > kMinF0 && prevF0 < kMaxF0) ? (dsr / prevF0) : 0.0;
        int best = -1;
        for (int tau = minLag; tau <= maxLag; ++tau) {
            if (diff[(size_t) tau] < kThreshold && diff[(size_t) tau] <= minDiff + 0.085f) {
                int t = tau;
                while (t + 1 <= maxLag && diff[(size_t) t + 1] < diff[(size_t) t]) ++t;

                // Octave-continuity bias: reject spurious half/double pitch jumps if a clean dip exists near prevF0
                if (expectedTau > 0.0) {
                    const double ratio = (double) t / expectedTau;
                    const bool isOctaveJump = std::abs(ratio - 2.0) < 0.14 || std::abs(ratio - 0.5) < 0.09;
                    if (isOctaveJump) {
                        const int nearIdx = std::clamp((int) std::lround(expectedTau), minLag, maxLag);
                        int localBest = nearIdx;
                        for (int k = std::max(minLag, nearIdx - 4); k <= std::min(maxLag, nearIdx + 4); ++k) {
                            if (diff[(size_t) k] < diff[(size_t) localBest]) localBest = k;
                        }
                        if (diff[(size_t) localBest] < kThreshold * 1.15f) {
                            best = localBest;
                            break;
                        }
                    }
                }
                best = t;
                break;
            }
        }
        if (best < 0 && minDiff < 0.30f) best = minTau;
        if (best < 0) {
            // no clear dip: report how periodic the best candidate was, as unvoiced
            e.aperiodicity = minDiff;
            e.confidence = std::clamp(1.0f - e.aperiodicity / 0.45f, 0.0f, 1.0f);
            e.candidate = (float) (minTau * decim);
            if (++unvoicedStreak > 40) prevF0 = 0.0f;
            return e;
        }
        double tau = best;
        if (best > minLag && best < maxLag) {
            double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
            double den = a - 2 * b + c;
            if (std::abs(den) > 1e-12) tau = best + 0.5 * (a - c) / den;
        }
        e.aperiodicity = std::clamp(diff[(size_t) best], 0.0f, 1.0f);
        e.confidence = std::clamp(1.0f - e.aperiodicity / 0.45f, 0.0f, 1.0f);
        e.period = (float) (tau * decim);
        if (e.period > 0.0f) {
            const float f0 = (float) (sr / e.period);
            if (f0 >= kMinF0 && f0 <= kMaxF0 && e.aperiodicity < 0.32f) {
                prevF0 = f0;
                unvoicedStreak = 0;
            } else {
                e.period = 0.0f;
                if (++unvoicedStreak > 40) prevF0 = 0.0f;
            }
        }
        return e;
    }

    double sampleRate() const { return sr; }

private:
    static constexpr float kThreshold = 0.20f;

    static int nextPow2(int n) {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    float at(int64_t i) const { return ring[(size_t) (i & mask)]; }

    void designLowpass(double normFreq) {
        const double w = 2.0 * M_PI * normFreq, q = 0.70710678;
        const double cw = std::cos(w), alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
        b0 = (1 - cw) / 2 / a0;
        b1 = (1 - cw) / a0;
        b2 = b0;
        a1 = -2 * cw / a0;
        a2 = (1 - alpha) / a0;
    }

    double biquad(float x, double& z1, double& z2) const {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    double sr = 48000, dsr = 12000;
    int decim = 4, window = 336, maxLag = 240, minLag = 14, phase = 0, mask = 0;
    int64_t written = 0, inCount = 0;
    std::vector<float> ring, diff;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1a = 0, z2a = 0, z1b = 0, z2b = 0;
    bool hpfEnabled = true;
    double hpfHz = 75.0;
    double hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0;
    double hz1 = 0, hz2 = 0;
    float prevF0 = 0.0f;
    int unvoicedStreak = 0;
};

} // namespace adub
