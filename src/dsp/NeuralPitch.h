// AlptugDUB by Alptuğ Dağ — neural pitch tracking, on the device.
//
// SwiftF0 (Lars Nieradzik, MIT licence; github.com/lars76/swift-f0) is a small convolutional network
// that reads three spectrograms of 16 kHz audio and scores 95 pitch steps from 47 Hz to 2.1 kHz, plus
// how sure it is that the frame is voiced. It is more accurate than CREPE in noise and makes almost
// no octave errors, which is what a pitch shifter needs most. Here it runs frame by frame as audio
// streams in: every layer keeps the few past columns it needs, so each 16 ms frame costs one column
// of each convolution (about 2.3 million multiply-adds) and nothing is allocated while running.
#pragma once

#include "PitchTracker.h"
#include "SwiftF0Model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace adub {

/** Band-limited conversion from the host's rate to 16 kHz (Kaiser-windowed sinc, about 100 dB). */
class Resampler16k {
public:
    void prepare(double sampleRate) {
        ratio = sampleRate / 16000.0;
        const double cutoff = 7200.0 / sampleRate; // cycles per input sample
        halfWidth = (int) std::ceil(20.0 * ratio);  // 20 output periods each side
        const double beta = 9.0;
        table.resize((size_t) (halfWidth * kPhases + 2));
        auto bessel0 = [](double x) {
            double sum = 1, term = 1;
            for (int k = 1; k < 30; ++k) {
                term *= (x / (2 * k)) * (x / (2 * k));
                sum += term;
            }
            return sum;
        };
        for (size_t i = 0; i < table.size(); ++i) {
            const double t = (double) i / kPhases; // input samples from the centre
            const double s = t == 0 ? 2 * cutoff : std::sin(2 * M_PI * cutoff * t) / (M_PI * t);
            const double u = t / halfWidth;
            const double w = u >= 1 ? 0 : bessel0(beta * std::sqrt(1 - u * u)) / bessel0(beta);
            table[i] = (float) (s * w);
        }
        int size = 1;
        while (size < 4 * halfWidth + 64) size <<= 1;
        ring.assign((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset() {
        std::fill(ring.begin(), ring.end(), 0.0f);
        written = 0;
        nextOut = 0;
    }

    /** Input samples between an input instant and the 16 kHz sample that describes it being ready. */
    int delay() const { return halfWidth + 1; }
    double inputPerOutput() const { return ratio; }

    /** Feeds one input sample; calls emit(y) for every 16 kHz sample that becomes ready. */
    template <typename Emit>
    void push(float x, Emit&& emit) {
        ring[(size_t) (written & mask)] = x;
        ++written;
        while (true) {
            const double centre = (double) nextOut * ratio;
            if (centre + halfWidth >= (double) written) return;
            const int64_t first = (int64_t) std::ceil(centre - halfWidth), last = (int64_t) std::floor(centre + halfWidth);
            double acc = 0;
            for (int64_t i = std::max<int64_t>(0, first); i <= last; ++i) acc += ring[(size_t) (i & mask)] * kernel(std::abs((double) i - centre));
            emit((float) acc);
            ++nextOut;
        }
    }

private:
    static constexpr int kPhases = 256;
    float kernel(double t) const {
        const double p = t * kPhases;
        const size_t i = (size_t) p;
        if (i + 1 >= table.size()) return 0.0f;
        const float f = (float) (p - (double) i);
        return table[i] + (table[i + 1] - table[i]) * f;
    }

    double ratio = 3;
    int halfWidth = 60;
    std::vector<float> table, ring;
    int64_t mask = 0, written = 0, nextOut = 0;
};

/** SwiftF0 on a stream of 16 kHz samples. */
class SwiftF0Stream {
public:
    static constexpr int kHop = 256;
    static constexpr int kBands = 128;
    static constexpr int kBins = 95;
    /** Output frames trail the newest computed column by this many frames. */
    static constexpr int kLagFrames = 7;
    /** 16 kHz samples between a frame's centre and the input that completes it. */
    static constexpr int kDelay = 1024 + kLagFrames * kHop;

    struct Frame {
        int64_t centre = 0;     // 16 kHz sample index of the frame centre
        float hz = 0;           // estimated pitch (also given for unvoiced frames)
        float confidence = 0;   // voiced when >= 0.5
        float rms = 0;          // level of the 32 ms around the centre
    };

    SwiftF0Stream() {
        for (int n : {512, 1024, 2048}) fftSetup(n);
        reset();
    }

    void reset() {
        audio.fill(0.0f);
        count = 0;
        std::fill(in.begin(), in.end(), 0.0f);
        for (auto* t : {&a, &h1, &b, &r1, &r2, &r3, &f, &g1, &g, &tmpA}) std::fill(t->begin(), t->end(), 0.0f);
        meanAt.fill(0.0f);
        confAt.fill(0.0f);
        std::fill(p.begin(), p.end(), 0.0f);
        std::fill(z.begin(), z.end(), 0.0f);
        std::fill(logits.begin(), logits.end(), 0.0f);
        rmsAt.fill(0.0f);
        peakAt.fill(0.0f);
    }

    /** Limits the search to a pitch range (Hz). */
    void setRange(float lowHz, float highHz) {
        fmin = lowHz;
        fmax = highHz;
    }

    /** Feeds one 16 kHz sample. Returns true when `out` holds a new frame. */
    bool push(float x, Frame& out) {
        audio[(size_t) (count & kAudioMask)] = x;
        ++count;
        if (count < 1024 || (count - 1024) % kHop != 0) return false;
        const int64_t t = (count - 1024) / kHop; // the column whose window just completed
        column(t);
        const int64_t tau = t - kLagFrames;
        if (tau < 0) return false;
        out = decode(tau);
        return true;
    }

private:
    // ---- tensors: a few columns of [channels][bands] per layer, indexed by frame mod kCols ----
    static constexpr int kCols = 8;
    static constexpr int64_t kAudioMask = 4095;
    template <int C, int F>
    struct Tensor {
        std::array<float, kCols * C * F> v{};
        float* col(int64_t t) { return v.data() + (size_t) ((t & (kCols - 1)) * C * F); }
        const float* col(int64_t t) const { return v.data() + (size_t) ((t & (kCols - 1)) * C * F); }
        float* begin() { return v.data(); }
        float* end() { return v.data() + v.size(); }
    };

    /**
     * One output column of a 2-D convolution over (band, time). The kernel is kf x kt with
     * dilation df along the bands; padTop zero bands above, and for kt = 3 the columns used are
     * t-1, t and t+1 of the input.
     */
    template <int Cin, int Fin, int Cout, int Fout>
    static void conv(const Tensor<Cin, Fin>& x, int64_t t, const float* W, const float* B, int kf, int kt, int df, int padTop,
                     float* out) {
        for (int o = 0; o < Cout; ++o) std::fill(out + o * Fout, out + (o + 1) * Fout, B[o]);
        for (int dt = 0; dt < kt; ++dt) {
            const float* src = x.col(t + dt - (kt - 1) / 2);
            for (int o = 0; o < Cout; ++o) {
                float* dst = out + o * Fout;
                for (int i = 0; i < Cin; ++i) {
                    const float* s = src + i * Fin;
                    for (int k = 0; k < kf; ++k) {
                        const float w = W[((o * Cin + i) * kf + k) * kt + dt];
                        const int shift = k * df - padTop;
                        const int lo = std::max(0, -shift), hi = std::min(Fout, Fin - shift);
                        for (int fb = lo; fb < hi; ++fb) dst[fb] += w * s[fb + shift];
                    }
                }
            }
        }
    }

    template <size_t N>
    static void relu(float* x) {
        for (size_t i = 0; i < N; ++i) x[i] = std::max(0.0f, x[i]);
    }

    /** A residual pair: x + conv(relu(conv(x))), then relu; both convolutions kf x 1. */
    void residual1(const Tensor<12, kBands>& x, Tensor<12, kBands>& out, int64_t t, const float* W1, const float* B1, const float* W2,
                   const float* B2, int kf, int df) {
        conv<12, kBands, 12, kBands>(x, t, W1, B1, kf, 1, df, (kf - 1) / 2 * df, tmpA.col(t));
        relu<12 * kBands>(tmpA.col(t));
        conv<12, kBands, 12, kBands>(tmpA, t, W2, B2, kf, 1, df, (kf - 1) / 2 * df, out.col(t));
        const float* xs = x.col(t);
        float* o = out.col(t);
        for (int i = 0; i < 12 * kBands; ++i) o[i] = std::max(0.0f, o[i] + xs[i]);
    }

    void column(int64_t t) {
        using namespace swiftf0;
        // ---- the three log spectrograms, centred on 16 kHz sample 256 t ----
        float* feat = in.col(t);
        spectrum(t, 1024, window1024, fb1024Start, fb1024Count, fb1024Weights, feat);
        spectrum(t, 2048, window2048, fb2048Start, fb2048Count, fb2048Weights, feat + kBands);
        spectrum(t, 512, window512, fb512Start, fb512Count, fb512Weights, feat + 2 * kBands);
        float mean = 0, peak = 0, energy = 0;
        for (int i = 0; i < 3 * kBands; ++i) mean += feat[i];
        meanAt[(size_t) (t & (kCols - 1))] = mean / (3 * kBands);
        for (int i = -256; i < 256; ++i) {
            const float v = sampleAt(t * kHop + i);
            peak = std::max(peak, std::abs(v));
            energy += v * v;
        }
        peakAt[(size_t) (t & (kCols - 1))] = peak;
        rmsAt[(size_t) (t & (kCols - 1))] = std::sqrt(energy / 512.0f);

        // ---- the network; each line notes the column it completes ----
        conv<3, kBands, 12, kBands>(in, t - 1, c0W, c0B, 3, 3, 1, 1, a.col(t - 1)); // t-1
        relu<12 * kBands>(a.col(t - 1));
        conv<12, kBands, 12, kBands>(a, t - 2, c1W, c1B, 7, 3, 1, 3, h1.col(t - 2)); // t-2
        relu<12 * kBands>(h1.col(t - 2));
        conv<12, kBands, 12, kBands>(h1, t - 3, c2W, c2B, 7, 3, 1, 3, b.col(t - 3)); // t-3
        {
            float* o = b.col(t - 3);
            const float* s = a.col(t - 3);
            for (int i = 0; i < 12 * kBands; ++i) o[i] = std::max(0.0f, o[i] + s[i]);
        }
        residual1(b, r1, t - 3, c3W, c3B, c4W, c4B, 7, 17);
        residual1(r1, r2, t - 3, c5W, c5B, c6W, c6B, 5, 27);
        residual1(r2, r3, t - 3, c7W, c7B, c8W, c8B, 3, 40);
        residual1(r3, f, t - 3, c9W, c9B, c10W, c10B, 3, 48);
        conv<12, kBands, 12, kBands>(f, t - 4, c11W, c11B, 7, 3, 2, 6, g1.col(t - 4)); // t-4
        relu<12 * kBands>(g1.col(t - 4));
        conv<12, kBands, 12, kBands>(g1, t - 5, c12W, c12B, 7, 3, 2, 6, g.col(t - 5)); // t-5
        {
            float* o = g.col(t - 5);
            const float* s = f.col(t - 5);
            for (int i = 0; i < 12 * kBands; ++i) o[i] = std::max(0.0f, o[i] + s[i]);
        }
        // the first 96 bands of g feed a 3x3 convolution with one zero band above, none below
        conv<12, kBands, 4, kBins>(g, t - 6, c13W, c13B, 3, 3, 1, 1, pcol(t - 6)); // t-6
        relu<4 * kBins>(pcol(t - 6));
        const float* pc = pcol(t - 6);
        float* lg = logits.data() + (size_t) (((t - 6) & (kCols - 1)) * kBins);
        for (int k = 0; k < kBins; ++k) {
            float s = headB[0];
            for (int c = 0; c < 4; ++c) s += headW[c] * pc[c * kBins + k];
            lg[k] = s;
        }
        // voicing: the loudest response of each of 4 maps, and the mean level of the spectrogram
        float q[4 * kBins];
        for (int o = 0; o < 4; ++o) {
            std::fill(q + o * kBins, q + (o + 1) * kBins, c14B[o]);
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 5; ++k) {
                    const float w = c14W[(o * 4 + i) * 5 + k];
                    const int shift = k - 2, lo = std::max(0, -shift), hi = std::min(kBins, kBins - shift);
                    for (int fb = lo; fb < hi; ++fb) q[o * kBins + fb] += w * pc[i * kBins + fb + shift];
                }
        }
        float* zc = z.data() + (size_t) (((t - 6) & (kCols - 1)) * 5);
        for (int o = 0; o < 4; ++o) zc[o] = *std::max_element(q + o * kBins, q + (o + 1) * kBins);
        zc[4] = meanAt[(size_t) ((t - 6) & (kCols - 1))];
        // the voicing head: 1-D convolution (k = 3) over time, then a 1 x 1, at t-7
        float hidden[16];
        for (int o = 0; o < 16; ++o) {
            float s = conf0B[o];
            for (int dt = 0; dt < 3; ++dt) {
                const float* zz = z.data() + (size_t) (((t - 8 + dt) & (kCols - 1)) * 5);
                for (int i = 0; i < 5; ++i) s += conf0W[(o * 5 + i) * 3 + dt] * zz[i];
            }
            hidden[o] = std::max(0.0f, s);
        }
        float c = conf1B[0];
        for (int o = 0; o < 16; ++o) c += conf1W[o] * hidden[o];
        confAt[(size_t) ((t - 7) & (kCols - 1))] = c;
    }

    Frame decode(int64_t tau) const {
        using namespace swiftf0;
        Frame fr;
        fr.centre = tau * kHop;
        const float* lg = logits.data() + (size_t) ((tau & (kCols - 1)) * kBins);
        int best = 0, bestInRange = -1;
        for (int k = 0; k < kBins; ++k) {
            if (lg[k] > lg[best]) best = k;
            if (binHz[k] >= fmin && binHz[k] <= fmax && (bestInRange < 0 || lg[k] > lg[bestInRange])) bestInRange = k;
        }
        if (bestInRange < 0) return fr;
        // pitch: softmax over the best step and its neighbours, averaged in log frequency
        double m = lg[bestInRange], wsum = 0, acc = 0;
        for (int d = -1; d <= 1; ++d) {
            const int k = bestInRange + d;
            if (k < 0 || k >= kBins || binHz[k] < fmin || binHz[k] > fmax) continue;
            const double w = std::exp((double) lg[k] - m);
            wsum += w;
            acc += w * logHz[k];
        }
        fr.hz = (float) std::exp(acc / wsum);
        const bool inRange = binHz[best] >= fmin && binHz[best] <= fmax;
        const float c = confAt[(size_t) (tau & (kCols - 1))];
        fr.confidence = inRange ? 1.0f / (1.0f + std::exp(-c)) : 0.0f;
        if (peakAt[(size_t) (tau & (kCols - 1))] < 1e-3f) fr.confidence = 0; // digital silence
        fr.rms = rmsAt[(size_t) (tau & (kCols - 1))];
        return fr;
    }

    float sampleAt(int64_t i) const { return i < 0 || i >= count ? 0.0f : audio[(size_t) (i & kAudioMask)]; }

    void spectrum(int64_t t, int n, const float* window, const int* start, const int* cnt, const float* weights, float* out) {
        auto& buf = fftBuf[(size_t) order(n)];
        const int64_t first = t * kHop - n / 2;
        for (int i = 0; i < n; ++i) buf[(size_t) i] = {sampleAt(first + i) * window[i], 0.0f};
        fft(buf, n);
        auto& mag = magBuf[(size_t) order(n)];
        for (int k = 0; k <= n / 2; ++k) mag[(size_t) k] = std::abs(buf[(size_t) k]);
        const float* w = weights;
        for (int band = 0; band < kBands; ++band) {
            float s = 0;
            for (int j = 0; j < cnt[band]; ++j) s += w[j] * mag[(size_t) (start[band] + j)];
            w += cnt[band];
            out[band] = std::log(s + 1e-8f);
        }
    }

    static int order(int n) { return n == 512 ? 0 : n == 1024 ? 1 : 2; }

    void fftSetup(int n) {
        const int o = order(n);
        fftBuf[(size_t) o].assign((size_t) n, {});
        magBuf[(size_t) o].assign((size_t) n / 2 + 1, 0.0f);
        twiddle[(size_t) o].resize((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k) twiddle[(size_t) o][(size_t) k] = std::polar(1.0f, (float) (-2.0 * M_PI * k / n));
    }

    void fft(std::vector<std::complex<float>>& x, int n) const {
        const auto& tw = twiddle[(size_t) order(n)];
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(x[(size_t) i], x[(size_t) j]);
        }
        for (int len = 2; len <= n; len <<= 1) {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < len / 2; ++j) {
                    const auto u = x[(size_t) (i + j)], v = x[(size_t) (i + j + len / 2)] * tw[(size_t) (j * step)];
                    x[(size_t) (i + j)] = u + v;
                    x[(size_t) (i + j + len / 2)] = u - v;
                }
        }
    }

    float* pcol(int64_t t) { return p.data() + (size_t) ((t & (kCols - 1)) * 4 * kBins); }

    std::array<float, 4096> audio{};
    int64_t count = 0;
    float fmin = 46.875f, fmax = 2093.75f;
    Tensor<3, kBands> in;
    Tensor<12, kBands> a, h1, b, r1, r2, r3, f, g1, g, tmpA;
    std::array<float, kCols * 4 * kBins> p{};
    std::array<float, kCols * kBins> logits{};
    std::array<float, kCols * 5> z{};
    std::array<float, kCols> meanAt{}, confAt{}, rmsAt{}, peakAt{};
    std::array<std::vector<std::complex<float>>, 3> fftBuf;
    std::array<std::vector<float>, 3> magBuf;
    std::array<std::vector<std::complex<float>>, 3> twiddle;
};

/** SwiftF0 at the host's sample rate: resamples, runs the network and reports pitch estimates. */
class NeuralPitch {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        resampler.prepare(sampleRate);
        net.setRange(50.0f, 1000.0f); // speech and singing; keeps the search away from rumble and whistles
        reset();
    }

    void reset() {
        resampler.reset();
        net.reset();
        fresh = false;
        latestEstimate = PitchEstimate{};
    }

    /** Host samples between an instant and its estimate being available. */
    int64_t delaySamples() const { return (int64_t) std::ceil(SwiftF0Stream::kDelay * resampler.inputPerOutput()) + resampler.delay() + 1; }
    /** Host samples between consecutive estimates. */
    int spacing() const { return (int) std::ceil(SwiftF0Stream::kHop * resampler.inputPerOutput()); }

    /** Feeds one host-rate sample (mono). Returns true when latest() holds a new estimate. */
    bool push(float x) {
        fresh = false;
        resampler.push(x, [this](float y) {
            SwiftF0Stream::Frame fr;
            if (net.push(y, fr)) {
                PitchEstimate e;
                e.time = (int64_t) std::llround((double) fr.centre * resampler.inputPerOutput());
                e.confidence = fr.confidence;
                e.rms = fr.rms;
                e.candidate = fr.hz > 0 ? (float) (sr / fr.hz) : 0.0f;
                if (fr.confidence >= 0.5f && fr.hz > 0) e.period = (float) (sr / fr.hz);
                e.aperiodicity = 0.6f * (1.0f - fr.confidence); // 0.3 at the voicing threshold
                latestEstimate = e;
                fresh = true;
            }
        });
        return fresh;
    }

    const PitchEstimate& latest() const { return latestEstimate; }

private:
    double sr = 48000;
    Resampler16k resampler;
    SwiftF0Stream net;
    PitchEstimate latestEstimate;
    bool fresh = false;
};

} // namespace adub
