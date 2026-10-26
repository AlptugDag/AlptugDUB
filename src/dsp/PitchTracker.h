// AlptugDUB by Alptuğ Dağ — voice pitch tracking.
//
// YIN (de Cheveigné & Kawahara, 2002) on a copy of the signal decimated to about 12 kHz: accurate for
// speech and singing between 50 Hz and 800 Hz, and cheap enough to run every 5 ms in real time.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace adub {

/** One estimate: the period at a point in time, or unvoiced. */
struct PitchEstimate {
    int64_t time = 0;       // input sample this estimate describes (its window centre)
    float period = 0.0f;    // in input samples; 0 when unvoiced
    float aperiodicity = 1; // YIN's d'(tau): 0 = perfectly periodic, 1 = noise
    float rms = 0.0f;
    float confidence = 0.0f; // neural tracker: probability that the frame is voiced
    float candidate = 0.0f;  // neural tracker: its best period even when unvoiced
    bool voiced() const { return period > 0.0f; }
};

class PitchTracker {
public:
    static constexpr float kMinF0 = 50.0f;
    static constexpr float kMaxF0 = 800.0f;

    void prepare(double sampleRate) {
        sr = sampleRate;
        decim = std::max(1, (int) std::lround(sampleRate / 12000.0));
        dsr = sampleRate / decim;
        window = (int) std::lround(dsr * 0.030);           // integration window: 30 ms
        maxLag = (int) std::ceil(dsr / kMinF0);
        minLag = std::max(2, (int) std::floor(dsr / kMaxF0));
        ring.assign(nextPow2(window + maxLag + 8) * 2, 0.0f);
        mask = (int) ring.size() - 1;
        diff.assign(maxLag + 2, 0.0f);
        // anti-alias: two biquads at 0.42 of the decimated Nyquist
        designLowpass(0.42 * dsr * 0.5 / sampleRate);
        reset();
    }

    void reset() {
        std::fill(ring.begin(), ring.end(), 0.0f);
        z1a = z2a = z1b = z2b = 0;
        phase = 0;
        written = 0;
        inCount = 0;
    }

    /** Feeds one input sample (mono). */
    void push(float x) {
        double y = biquad(x, z1a, z2a);
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

    /** Analyses the newest window. */
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
        if (e.rms < 1.0e-4f) return e; // about -80 dBFS: silence

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
            diff[(size_t) tau] = running > 0 ? (float) (d * tau / running) : 1.0f;
        }
        int best = -1;
        for (int tau = minLag; tau <= maxLag; ++tau) {
            if (diff[(size_t) tau] < kThreshold) {
                while (tau + 1 <= maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) {
            // no clear dip: report how periodic the best candidate was, as unvoiced
            float m = 1.0f;
            for (int tau = minLag; tau <= maxLag; ++tau) m = std::min(m, diff[(size_t) tau]);
            e.aperiodicity = m;
            return e;
        }
        double tau = best;
        if (best > minLag && best < maxLag) {
            double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
            double den = a - 2 * b + c;
            if (std::abs(den) > 1e-12) tau = best + 0.5 * (a - c) / den;
        }
        e.aperiodicity = diff[(size_t) best];
        e.period = (float) (tau * decim);
        return e;
    }

    double sampleRate() const { return sr; }

private:
    static constexpr float kThreshold = 0.3f;

    static int nextPow2(int n) {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    float at(int64_t i) const { return ring[(size_t) (i & mask)]; }

    void designLowpass(double normFreq) {
        const double w = 2.0 * M_PI * normFreq, q = 0.7071;
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
    int decim = 4, window = 360, maxLag = 185, minLag = 15, phase = 0, mask = 0;
    int64_t written = 0, inCount = 0;
    std::vector<float> ring, diff;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1a = 0, z2a = 0, z1b = 0, z2b = 0;
};

} // namespace adub
