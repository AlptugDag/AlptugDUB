// AlptugDUB by Alptuğ Dağ — natural voice pitch and formant shifting.
//
// Real-time TD-PSOLA. The voice is cut into grains, one per glottal pulse (pitch marks found on the
// signal itself, guided by the pitch analysis). Re-spacing the grains changes the pitch while every
// grain keeps its own spectral envelope, so formants stay where they were; resampling each grain moves
// the formants independently (a longer or shorter vocal tract). Unvoiced sounds (s, ş, f, breaths)
// are never re-pitched, so they keep their texture. Mono analysis drives every channel, so a stereo
// voice stays phase-coherent.
//
// Nothing here is a fixed effect: every grain is shaped from that pulse's own pitch and loudness.
// The pitch is mapped around the speaker's centre (so the melody can be widened, flattened or
// steadied), the vocal tract can move with the voice the way a real larynx does, and growl, age and
// tremor follow how hard the speaker is pushing.
#pragma once

#include "PitchTracker.h"
#include "Ring.h"

#include <cmath>
#include <vector>

namespace adub {

class PsolaShifter {
public:
    static constexpr float kMinF0 = 50.0f;

    /**
     * maxAnalysisDelay: the longest delay any pitch analysis will have (host samples), so the buffers
     * fit every mode without reallocating later.
     */
    void prepare(double sampleRate, int numChannels, int64_t maxAnalysisDelay) {
        sr = sampleRate;
        channels = std::max(1, numChannels);
        maxT = (float) (sampleRate / kMinF0);
        unvoicedT = (float) (sampleRate * 0.006);
        const int maxLatency = latencyFor(maxAnalysisDelay, (int) (sampleRate * 0.02));
        int size = 1;
        while (size < maxLatency * 2 + (int) (8 * maxT)) size <<= 1;
        mask = size - 1;
        in.assign((size_t) channels, std::vector<float>((size_t) size, 0.0f));
        out.assign((size_t) channels, std::vector<float>((size_t) size, 0.0f));
        wsum.assign((size_t) size, 0.0f);
        lp.assign((size_t) size, 0.0f);
        env.assign((size_t) size, 0.0f);
        // 1 kHz low-pass for locating glottal pulses
        const double w = 2.0 * M_PI * 1000.0 / sampleRate, cw = std::cos(w), alpha = std::sin(w) / (2 * 0.7071);
        const double a0 = 1 + alpha;
        lb0 = (1 - cw) / 2 / a0; lb1 = (1 - cw) / a0; lb2 = lb0; la1 = -2 * cw / a0; la2 = (1 - alpha) / a0;
        envAttack = (float) (1.0 - std::exp(-1.0 / (0.002 * sampleRate)));
        envRelease = (float) (1.0 - std::exp(-1.0 / (0.050 * sampleRate)));
        slowCoef = (float) (1.0 - std::exp(-1.0 / (1.5 * sampleRate)));
        setAnalysis((int64_t) (sampleRate * 0.03), (int) (sampleRate * 0.005));
    }

    /**
     * Where pitch estimates come from: how far they trail the audio and how often they arrive.
     * Clears the stream (call between blocks, when the analysis changes).
     */
    void setAnalysis(int64_t delaySamples, int spacingSamples) {
        analysisDelay = delaySamples;
        spacing = std::max(1, spacingSamples);
        latency = latencyFor(analysisDelay, spacing);
        reset();
    }

    void reset() {
        for (auto& c : in) std::fill(c.begin(), c.end(), 0.0f);
        for (auto& c : out) std::fill(c.begin(), c.end(), 0.0f);
        std::fill(wsum.begin(), wsum.end(), 0.0f);
        std::fill(lp.begin(), lp.end(), 0.0f);
        std::fill(env.begin(), env.end(), 0.0f);
        lz1 = lz2 = 0;
        estimates.clear();
        marks.clear();
        inCount = 0;
        lastMark = -1;
        synthPos = 0;
        synthAcc = 0;
        markIndex = 0;
        rmsIn = rmsOut = 0;
        agc = 1;
        envNow = 0;
        slowLevel = 1e-4f;
        smoothLogF0 = 0;
        wasVoiced = false;
    }

    /** Samples of delay this processor adds (report to the host). */
    int latencySamples() const { return latency; }
    /** The delay it would add with a given analysis (see setAnalysis). */
    int latencyForAnalysis(int64_t delaySamples, int spacingSamples) const { return latencyFor(delaySamples, std::max(1, spacingSamples)); }

    /** A pitch estimate; `time` counts samples from the start of the stream given to process(). */
    void pushEstimate(const PitchEstimate& e) { estimates.push_back(e); }

    /** Pitch and formant ratios (2 = an octave up). Safe to call between blocks. */
    void setRatios(float pitch, float formant) {
        pitchRatio = std::clamp(pitch, 0.35f, 2.9f);
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

    /**
     * How the voice moves:
     *  response   0..1, growl, shimmer and tremor follow the speaker's effort (loud parts rougher)
     *  tract      0..1, the vocal tract rises and falls with the pitch, as a real larynx does
     *  stability  0..1, smooths the pitch from pulse to pulse (steadier, more controlled voice)
     */
    void setDynamics(float response, float tract, float stability) {
        responseAmount = std::clamp(response, 0.0f, 1.0f);
        tractAmount = std::clamp(tract, 0.0f, 1.0f);
        stabilityAmount = std::clamp(stability, 0.0f, 1.0f);
    }

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
            const float a = std::abs(mono);
            envNow += (a - envNow) * (a > envNow ? envAttack : envRelease);
            env[(size_t) (inCount & mask)] = envNow;
            ++inCount;
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
        int64_t pos = 0;
        float period = 0;
        bool voiced = false;
    };

    static constexpr float kMinFormantRatio = 0.63f; // -8 semitones

    int latencyFor(int64_t delay, int estimateSpacing) const {
        // The lowest formant ratio sets the longest output grain; marks need 1.3 periods of signal
        // after them, and the analysis must have reached the far end of the next grain.
        const double maxGrainHalf = maxT / kMinFormantRatio;
        return (int) std::ceil(maxGrainHalf + 2.4 * maxT + (double) delay + estimateSpacing + 16);
    }

    float agcCoef() const { return (float) (1.0 / (0.3 * sr)); }

    /** The pitch at a time: the nearest estimate, its period blended with the neighbour on the other side. */
    PitchEstimate estimateAt(int64_t time) const {
        if (estimates.empty()) return {};
        size_t best = 0;
        int64_t bestDist = INT64_MAX;
        for (size_t i = estimates.size(); i-- > 0;) {
            const int64_t d = std::llabs(estimates[i].time - time);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            } else if (estimates[i].time < time) {
                break;
            }
        }
        PitchEstimate e = estimates[best];
        const size_t other = e.time <= time ? best + 1 : (best > 0 ? best - 1 : best);
        if (other != best && other < estimates.size() && e.voiced() && estimates[other].voiced()) {
            const PitchEstimate& o = estimates[other];
            const double span = (double) (o.time - e.time);
            const double u = span != 0 ? std::clamp((double) (time - e.time) / span, 0.0, 1.0) : 0.0;
            e.period = (float) std::exp(std::log(e.period) * (1 - u) + std::log(o.period) * u);
        }
        return e;
    }

    void placeMarks() {
        // Pitch estimates exist up to `horizon`; a mark needs 1.3 periods of signal after it.
        const int64_t horizon = inCount - analysisDelay - spacing - (int64_t) (1.3f * maxT) - 2;
        if (lastMark < 0) {
            if (horizon <= 0) return;
            lastMark = 0;
            marks.push_back({0, unvoicedT, false});
        }
        while (true) {
            PitchEstimate e = estimateAt(lastMark);
            const bool voiced = e.voiced() && e.aperiodicity < 0.35f && e.period <= maxT;
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
        while (estimates.size() > 2 && estimates[1].time < synthPos - 8 * (int64_t) maxT) estimates.pop_front();
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
            if (markIndex + 1 >= marks.size()) break; // wait for input
            const Mark& m = marks[markIndex];
            const Mark& nx = marks[markIndex + 1];
            // Each grain spans from the previous pulse to the next one (asymmetric Hann), so with no
            // shift the grains add back to exactly the input.
            const float left = std::max(2.0f, m.period), right = std::max(2.0f, (float) (nx.pos - m.pos));
            float step, fr, gain = 1.0f;
            int64_t offset = 0;
            if (m.voiced) {
                const float f0 = (float) sr / right;
                // effort: this pulse's loudness against the speaker's recent average
                const float level = env[(size_t) (m.pos & mask)];
                slowLevel += (level - slowLevel) * std::min(1.0f, slowCoef * right);
                const float effort = std::clamp(level / std::max(1e-5f, slowLevel), 0.2f, 2.5f);
                const float drive = 1.0f + responseAmount * (std::min(effort, 1.8f) - 1.0f); // 1 = average effort

                float ratio = pitchRatio;
                if (intonation != 1.0f && centreF0 > 0.0f) ratio *= std::pow(centreF0 / f0, 1.0f - intonation);
                // stability: follow a smoothed pitch contour instead of every pulse's own pitch
                const float logF0 = std::log(f0);
                if (!wasVoiced) smoothLogF0 = logF0;
                smoothLogF0 += (logF0 - smoothLogF0) * (1.0f - 0.92f * stabilityAmount);
                if (stabilityAmount > 0.0f) ratio *= std::exp(stabilityAmount * (smoothLogF0 - logF0));
                wasVoiced = true;
                if (ageAmount > 0.0f) {
                    const float a = ageAmount * std::clamp(drive, 0.4f, 1.6f);
                    tremor += 2.0 * M_PI * 5.5 * right / sr;
                    if (tremor > 2.0 * M_PI) tremor -= 2.0 * M_PI;
                    ratio *= std::pow(2.0f, a * 0.45f / 12.0f * (float) std::sin(tremor));
                    ratio *= 1.0f + a * 0.025f * noise(); // jitter
                    gain *= 1.0f + a * 0.18f * noise();   // shimmer
                }
                const float g = std::clamp(growlAmount * drive, 0.0f, 1.0f);
                if (g > 0.0f && (parity ^= 1)) {
                    gain *= 1.0f - g * (0.5f + 0.2f * noise());
                    offset = (int64_t) std::lround(g * (0.14f + 0.05f * noise()) * right);
                }
                // keep the result a plausible voice, whatever the input does
                const float outF0 = f0 * ratio;
                if (outF0 > 1000.0f) ratio *= 1000.0f / outF0;
                if (outF0 < 40.0f) ratio *= 40.0f / outF0;
                step = right / std::clamp(ratio, 0.33f, 3.0f);
                fr = formantRatio;
                if (tractAmount > 0.0f && centreF0 > 0.0f)
                    fr *= std::clamp(std::pow(f0 / centreF0, 0.2f * tractAmount), 0.89f, 1.12f); // at most 2 st either way
                fr = std::clamp(fr, kMinFormantRatio, 1.6f);
            } else {
                wasVoiced = false;
                step = right;
                fr = 1.0f + (formantRatio - 1.0f) * 0.5f; // fricatives move half as far
            }
            const int hl = std::max(2, (int) std::lround(left / fr)), hr = std::max(2, (int) std::lround(right / fr));
            for (int k = -hl; k <= hr; ++k) {
                const float u = k < 0 ? (float) k / (float) hl : (float) k / (float) hr;
                const float w = 0.5f * (1.0f + std::cos((float) M_PI * u));
                const double src = (double) m.pos + (double) k * fr;
                const size_t dst = (size_t) ((synthPos + offset + k) & mask);
                for (int c = 0; c < channels; ++c) {
                    // Reading the grain faster than real time (formants up) folds high frequencies back
                    // down as hiss; a short box average over the stride keeps them out.
                    const float v = fr > 1.08f ? (sample(c, src - 0.34 * (double) fr) + sample(c, src) + sample(c, src + 0.34 * (double) fr)) * (1.0f / 3.0f)
                                               : sample(c, src);
                    out[(size_t) c][dst] += v * w * gain;
                }
                wsum[dst] += w;
            }
            synthAcc += step;
            const int64_t adv = (int64_t) std::floor(synthAcc);
            synthAcc -= (double) adv;
            synthPos += std::max<int64_t>(1, adv);
        }
    }

    double sr = 48000;
    int channels = 1, latency = 0, spacing = 240;
    int64_t analysisDelay = 0;
    int64_t mask = 0;
    float maxT = 960, unvoicedT = 288;
    float pitchRatio = 1, formantRatio = 1;
    float intonation = 1, growlAmount = 0, ageAmount = 0, centreF0 = 0;
    float responseAmount = 0, tractAmount = 0, stabilityAmount = 0;
    float envNow = 0, envAttack = 0.01f, envRelease = 0.001f, slowLevel = 1e-4f, slowCoef = 1e-5f;
    float smoothLogF0 = 0;
    bool wasVoiced = false;
    double tremor = 0;
    int parity = 0;
    uint32_t rng = 0x9E3779B9u;
    bool matchLoudness = true;
    std::vector<std::vector<float>> in, out;
    std::vector<float> wsum, lp, env;
    double lb0 = 1, lb1 = 0, lb2 = 0, la1 = 0, la2 = 0, lz1 = 0, lz2 = 0;
    Ring<PitchEstimate, 1024> estimates;
    Ring<Mark, 4096> marks; // a few hundred are ever live; the rest is headroom for 192 kHz
    size_t markIndex = 0;
    int64_t inCount = 0, lastMark = -1, synthPos = 0;
    double synthAcc = 0;
    float rmsIn = 0, rmsOut = 0, agc = 1;
};

} // namespace adub
