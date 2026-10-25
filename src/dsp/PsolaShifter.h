// AlptugDUB by Alptuğ Dağ — natural voice pitch and formant shifting.
//
// Real-time TD-PSOLA. The voice is cut into grains, one per glottal pulse (pitch marks found on the
// signal itself, guided by the pitch tracker). Re-spacing the grains changes the pitch while every
// grain keeps its own spectral envelope, so formants stay where they were; resampling each grain moves
// the formants independently (a longer or shorter vocal tract). Unvoiced sounds (s, ş, f, breaths)
// are never re-pitched, so they keep their texture. Mono analysis drives every channel, so a stereo
// voice stays phase-coherent.
#pragma once

#include "PitchTracker.h"

#include <cmath>
#include <deque>
#include <vector>

namespace adub {

class PsolaShifter {
public:
    void prepare(double sampleRate, int numChannels) {
        sr = sampleRate;
        channels = std::max(1, numChannels);
        tracker.prepare(sampleRate);
        maxT = (float) (sampleRate / PitchTracker::kMinF0);
        unvoicedT = (float) (sampleRate * 0.006);
        hop = std::max(1, (int) std::lround(sampleRate * 0.005));
        // Lowest formant ratio supported (-8 semitones) sets the longest output grain.
        const double maxGrainHalf = maxT / kMinFormantRatio;
        latency = (int) std::ceil(maxGrainHalf + 2.4 * maxT + (double) tracker.delaySamples() + hop + 16);
        int size = 1;
        while (size < latency * 2 + (int) (8 * maxT)) size <<= 1;
        ringSize = size;
        mask = size - 1;
        in.assign((size_t) channels, std::vector<float>((size_t) size, 0.0f));
        out.assign((size_t) channels, std::vector<float>((size_t) size, 0.0f));
        wsum.assign((size_t) size, 0.0f);
        lp.assign((size_t) size, 0.0f);
        // 1 kHz low-pass for locating glottal pulses
        const double w = 2.0 * M_PI * 1000.0 / sampleRate, cw = std::cos(w), alpha = std::sin(w) / (2 * 0.7071);
        const double a0 = 1 + alpha;
        lb0 = (1 - cw) / 2 / a0; lb1 = (1 - cw) / a0; lb2 = lb0; la1 = -2 * cw / a0; la2 = (1 - alpha) / a0;
        reset();
    }

    void reset() {
        tracker.reset();
        for (auto& c : in) std::fill(c.begin(), c.end(), 0.0f);
        for (auto& c : out) std::fill(c.begin(), c.end(), 0.0f);
        std::fill(wsum.begin(), wsum.end(), 0.0f);
        std::fill(lp.begin(), lp.end(), 0.0f);
        lz1 = lz2 = 0;
        estimates.clear();
        marks.clear();
        inCount = 0;
        lastMark = -1;
        synthPos = 0;
        markIndex = 0;
        last = PitchEstimate{};
        rmsIn = rmsOut = 0;
        agc = 1;
    }

    /** Samples of delay this processor adds (report to the host). */
    int latencySamples() const { return latency; }

    /** Pitch and formant ratios (2 = an octave up). Safe to call between blocks. */
    void setRatios(float pitch, float formant) {
        pitchRatio = std::clamp(pitch, 0.5f, 2.0f);
        formantRatio = std::clamp(formant, kMinFormantRatio, 1.6f);
    }

    void setLoudnessMatch(bool on) { matchLoudness = on; }

    /**
     * Voice character beyond pitch and formants, all made from the glottal pulses themselves:
     *  range   1 = the speaker's own melody, <1 flatter (a monster), >1 livelier (a child)
     *  growl   0..1, period doubling: every other pulse quieter and late, the creak of vocal fry
     *  age     0..1, jitter and shimmer between pulses plus a slow tremor
     *  medianF0  the speaker's middle pitch (Hz), the centre that `range` scales around
     */
    void setCharacter(float range, float growl, float age, float medianF0) {
        intonation = std::clamp(range, 0.3f, 2.0f);
        growlAmount = std::clamp(growl, 0.0f, 1.0f);
        ageAmount = std::clamp(age, 0.0f, 1.0f);
        centreF0 = medianF0;
    }

    /** The newest pitch estimate, for meters and the voice profile. */
    const PitchEstimate& latest() const { return last; }

    /** Processes a block in place. data[ch][i], n samples. */
    void process(float* const* data, int n) {
        for (int i = 0; i < n; ++i) {
            float mono = 0;
            for (int c = 0; c < channels; ++c) {
                in[(size_t) c][(size_t) (inCount & mask)] = data[c][i];
                mono += data[c][i];
            }
            mono /= (float) channels;
            double y = lb0 * mono + lz1;
            lz1 = lb1 * mono - la1 * y + lz2;
            lz2 = lb2 * mono - la2 * y;
            lp[(size_t) (inCount & mask)] = (float) y;
            tracker.push(mono);
            ++inCount;
            if (inCount % hop == 0) {
                last = tracker.estimate();
                estimates.push_back(last);
                while (estimates.size() > 400) estimates.pop_front();
            }
        }
        placeMarks();
        const int64_t emitEnd = inCount - latency;
        synthesise(emitEnd);
        // read the finished output, delayed by `latency`
        int64_t t = emitEnd - n;
        for (int i = 0; i < n; ++i, ++t) {
            const size_t k = (size_t) (t & mask);
            const float norm = 1.0f / std::max(1.0f, wsum[k]);
            float o0 = 0, i0 = 0;
            for (int c = 0; c < channels; ++c) {
                float v = t >= 0 ? out[(size_t) c][k] * norm : 0.0f;
                out[(size_t) c][k] = 0;
                if (c == 0) {
                    o0 = v;
                    i0 = t >= 0 ? in[0][k] : 0.0f;
                }
                data[c][i] = v;
            }
            wsum[k] = 0;
            // slow loudness match: the shifted voice keeps the original's level
            rmsIn += (i0 * i0 - rmsIn) * agcCoef();
            rmsOut += (o0 * o0 - rmsOut) * agcCoef();
            if (matchLoudness) {
                float target = rmsIn > 1e-7f && rmsOut > 1e-9f ? std::sqrt(rmsIn / rmsOut) : 1.0f;
                target = std::clamp(target, 0.5f, 2.0f);
                agc += (target - agc) * agcCoef() * 0.5f;
                for (int c = 0; c < channels; ++c) data[c][i] *= agc;
            }
        }
    }

private:
    struct Mark {
        int64_t pos;
        float period;
        bool voiced;
    };

    static constexpr float kMinFormantRatio = 0.63f; // -8 semitones

    float agcCoef() const { return (float) (1.0 / (0.3 * sr)); }

    /** The pitch estimate closest to a time. */
    PitchEstimate estimateAt(int64_t time) const {
        PitchEstimate best;
        int64_t bestDist = INT64_MAX;
        for (auto it = estimates.rbegin(); it != estimates.rend(); ++it) {
            int64_t d = std::llabs(it->time - time);
            if (d < bestDist) {
                bestDist = d;
                best = *it;
            } else if (it->time < time) {
                break;
            }
        }
        return best;
    }

    void placeMarks() {
        // Pitch estimates exist up to `horizon`; a mark needs one period of signal after it.
        const int64_t horizon = inCount - tracker.delaySamples() - (int64_t) (1.3f * maxT) - 2;
        if (lastMark < 0) {
            if (horizon <= 0) return;
            lastMark = 0;
            marks.push_back({0, unvoicedT, false});
        }
        while (true) {
            PitchEstimate e = estimateAt(lastMark);
            const bool voiced = e.voiced() && e.aperiodicity < 0.3f;
            float T = voiced ? e.period : unvoicedT;
            int64_t next = lastMark + (int64_t) std::lround(T);
            if (next > horizon) break;
            if (voiced) {
                // the glottal pulse: largest low-passed peak near one period ahead
                const int64_t lo = lastMark + (int64_t) (0.8f * T), hi = lastMark + (int64_t) (1.2f * T);
                float bestV = -1e9f;
                for (int64_t p = lo; p <= hi; ++p) {
                    float v = lp[(size_t) (p & mask)];
                    if (v > bestV) {
                        bestV = v;
                        next = p;
                    }
                }
                T = (float) (next - lastMark);
            }
            marks.push_back({next, T, voiced});
            lastMark = next;
        }
        // keep marks that grains may still use
        while (marks.size() > 2 && markIndex > 1 && marks.front().pos < synthPos - 4 * (int64_t) maxT) {
            marks.pop_front();
            --markIndex;
        }
    }

    /** Uniform noise in [-1, 1) (xorshift, real-time safe). */
    float noise() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float) (rng & 0xFFFFFF) / 8388608.0f - 1.0f;
    }

    float sample(int c, double pos) const {
        // cubic (Catmull-Rom) interpolation
        const int64_t i = (int64_t) std::floor(pos);
        const float f = (float) (pos - (double) i);
        const auto& b = in[(size_t) c];
        float y0 = b[(size_t) ((i - 1) & mask)], y1 = b[(size_t) (i & mask)];
        float y2 = b[(size_t) ((i + 1) & mask)], y3 = b[(size_t) ((i + 2) & mask)];
        float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float a2 = -0.5f * y0 + 0.5f * y2;
        return ((a0 * f + a1) * f + a2) * f + y1;
    }

    void synthesise(int64_t emitEnd) {
        if (marks.empty()) return;
        const float maxHalf = maxT / kMinFormantRatio;
        while (synthPos - (int64_t) maxHalf <= emitEnd) {
            // the analysis mark nearest to this synthesis instant
            while (markIndex + 1 < marks.size() &&
                   std::llabs(marks[markIndex + 1].pos - synthPos) <= std::llabs(marks[markIndex].pos - synthPos))
                ++markIndex;
            const Mark& m = marks[markIndex];
            if (markIndex + 1 >= marks.size()) break; // wait for input
            const Mark& nx = marks[markIndex + 1];
            // Each grain spans from the previous pulse to the next one (asymmetric Hann), so with no
            // shift the grains add back to exactly the input.
            const float left = std::max(2.0f, m.period), right = std::max(2.0f, (float) (nx.pos - m.pos));
            float step, fr, gain = 1.0f;
            int64_t offset = 0;
            if (m.voiced) {
                float ratio = pitchRatio;
                if (intonation != 1.0f && centreF0 > 0.0f)
                    ratio *= std::pow(centreF0 / (float) (sr / right), 1.0f - intonation);
                if (ageAmount > 0.0f) {
                    tremor += 2.0 * M_PI * 5.5 * right / sr;
                    if (tremor > 2.0 * M_PI) tremor -= 2.0 * M_PI;
                    ratio *= std::pow(2.0f, ageAmount * 0.45f / 12.0f * (float) std::sin(tremor));
                    ratio *= 1.0f + ageAmount * 0.025f * noise();        // jitter
                    gain *= 1.0f + ageAmount * 0.18f * noise();          // shimmer
                }
                if (growlAmount > 0.0f && (parity ^= 1)) {
                    gain *= 1.0f - growlAmount * (0.5f + 0.2f * noise());
                    offset = (int64_t) std::lround(growlAmount * (0.14f + 0.05f * noise()) * right);
                }
                step = right / std::clamp(ratio, 0.45f, 2.2f);
                fr = formantRatio;
            } else {
                step = right;
                fr = 1.0f + (formantRatio - 1.0f) * 0.5f; // fricatives move half as far
            }
            const int hl = std::max(2, (int) std::lround(left / fr)), hr = std::max(2, (int) std::lround(right / fr));
            for (int k = -hl; k <= hr; ++k) {
                const float u = k < 0 ? (float) k / (float) hl : (float) k / (float) hr;
                const float w = 0.5f * (1.0f + std::cos((float) M_PI * u));
                const double src = (double) m.pos + (double) k * fr;
                const size_t dst = (size_t) ((synthPos + offset + k) & mask);
                for (int c = 0; c < channels; ++c) out[(size_t) c][dst] += sample(c, src) * w * gain;
                wsum[dst] += w;
            }
            synthAcc += step;
            const int64_t adv = (int64_t) std::floor(synthAcc);
            synthAcc -= (double) adv;
            synthPos += std::max<int64_t>(1, adv);
        }
    }

    PitchTracker tracker;
    double sr = 48000;
    int channels = 1, hop = 240, latency = 0, ringSize = 0;
    int64_t mask = 0;
    float maxT = 738, unvoicedT = 288;
    float pitchRatio = 1, formantRatio = 1;
    float intonation = 1, growlAmount = 0, ageAmount = 0, centreF0 = 0;
    double tremor = 0;
    int parity = 0;
    uint32_t rng = 0x9E3779B9u;
    bool matchLoudness = true;
    std::vector<std::vector<float>> in, out;
    std::vector<float> wsum, lp;
    double lb0 = 1, lb1 = 0, lb2 = 0, la1 = 0, la2 = 0, lz1 = 0, lz2 = 0;
    std::deque<PitchEstimate> estimates;
    std::deque<Mark> marks;
    size_t markIndex = 0;
    int64_t inCount = 0, lastMark = -1, synthPos = 0;
    double synthAcc = 0;
    PitchEstimate last;
    float rmsIn = 0, rmsOut = 0, agc = 1;
};

} // namespace adub
