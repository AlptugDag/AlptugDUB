// AlptugDUB test bench: runs a WAV file through the voice engine and measures the result.
//
//   dubtest in.wav out.wav <pitch semitones> <formant semitones>
//
// Prints the learned voice profile of the input and the output, the pitch change actually achieved,
// the formant movement, clicks (sample jumps the input never has) and the level change.
#include "../src/dsp/PsolaShifter.h"
#include "../src/dsp/VoiceProfile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <algorithm>
#include <complex>
#include <vector>

struct Wav {
    int sr = 48000, ch = 1;
    std::vector<std::vector<float>> data;
};

static bool readWav(const char* path, Wav& w) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char riff[12];
    f.read(riff, 12);
    int bits = 16, fmt = 1;
    while (f) {
        char id[4];
        uint32_t size = 0;
        f.read(id, 4);
        f.read((char*) &size, 4);
        if (!f) break;
        if (!std::memcmp(id, "fmt ", 4)) {
            std::vector<char> b(size);
            f.read(b.data(), size);
            fmt = *(uint16_t*) &b[0];
            w.ch = *(uint16_t*) &b[2];
            w.sr = (int) *(uint32_t*) &b[4];
            bits = *(uint16_t*) &b[14];
        } else if (!std::memcmp(id, "data", 4)) {
            std::vector<char> b(size);
            f.read(b.data(), size);
            const int bytes = bits / 8, frames = (int) (size / (bytes * w.ch));
            w.data.assign((size_t) w.ch, std::vector<float>((size_t) frames));
            for (int i = 0; i < frames; ++i)
                for (int c = 0; c < w.ch; ++c) {
                    const char* p = &b[(size_t) ((i * w.ch + c) * bytes)];
                    float v = 0;
                    if (fmt == 3 && bits == 32) v = *(const float*) p;
                    else if (bits == 16) v = *(const int16_t*) p / 32768.0f;
                    else if (bits == 24) v = (float) (((p[2] << 24) | ((uint8_t) p[1] << 16) | ((uint8_t) p[0] << 8)) >> 8) / 8388608.0f;
                    w.data[(size_t) c][(size_t) i] = v;
                }
            return true;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);
        }
    }
    return false;
}

static void writeWav(const char* path, const Wav& w) {
    std::ofstream f(path, std::ios::binary);
    const uint32_t frames = (uint32_t) w.data[0].size(), bytes = frames * w.ch * 4;
    auto u32 = [&](uint32_t v) { f.write((const char*) &v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16((uint16_t) w.ch); u32((uint32_t) w.sr);
    u32((uint32_t) (w.sr * w.ch * 4)); u16((uint16_t) (w.ch * 4)); u16(32);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < w.ch; ++c) f.write((const char*) &w.data[(size_t) c][i], 4);
}


// Median F0 over voiced frames, exact (not binned).
static float medianF0(const Wav& w) {
    adub::PitchTracker t;
    t.prepare(w.sr);
    const int hop = (int) std::lround(w.sr * 0.005);
    std::vector<float> f;
    for (size_t i = 0; i < w.data[0].size(); ++i) {
        t.push(w.data[0][i]);
        if ((i + 1) % (size_t) hop == 0) {
            auto e = t.estimate();
            if (e.voiced() && e.rms > 0.003f) f.push_back((float) (w.sr / e.period));
        }
    }
    if (f.empty()) return 0;
    std::nth_element(f.begin(), f.begin() + f.size() / 2, f.end());
    return f[f.size() / 2];
}

static void fftc(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / (double) len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> wv(1, 0);
            for (size_t j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * wv;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                wv *= wl;
            }
        }
    }
}

// Long-term spectral envelope on a log-frequency grid (1/24 octave, 150 Hz .. 5 kHz), smoothed over
// a third of an octave so harmonics vanish and only the formant structure remains.
static std::vector<double> envelope(const Wav& w) {
    const int N = 2048, bins = 24 * 5 + 1; // 5 octaves from 150 Hz
    std::vector<double> ltas(N / 2, 0.0);
    const auto& x = w.data[0];
    for (size_t pos = 0; pos + N < x.size(); pos += N / 2) {
        double e = 0;
        for (int i = 0; i < N; ++i) e += x[pos + i] * x[pos + i];
        if (e / N < 1e-5) continue;
        std::vector<std::complex<double>> a(N);
        for (int i = 0; i < N; ++i) a[(size_t) i] = x[pos + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1)));
        fftc(a);
        for (int k = 0; k < N / 2; ++k) ltas[(size_t) k] += std::norm(a[(size_t) k]);
    }
    std::vector<double> g(bins);
    for (int b = 0; b < bins; ++b) {
        const double fc = 150.0 * std::pow(2.0, b / 24.0), lo = fc * std::pow(2.0, -1.0 / 6), hi = fc * std::pow(2.0, 1.0 / 6);
        double acc = 0;
        int n = 0;
        for (int k = (int) (lo * N / w.sr); k <= (int) (hi * N / w.sr) && k < N / 2; ++k, ++n) acc += ltas[(size_t) k];
        g[(size_t) b] = 10 * std::log10(acc / std::max(1, n) + 1e-20);
    }
    return g;
}

// Semitones the output envelope is shifted against the input's (best correlation, 1/8 st steps).
static float envelopeShift(const std::vector<double>& a, const std::vector<double>& b) {
    float best = 0;
    double bestC = -1e18;
    for (int s8 = -64; s8 <= 64; ++s8) {
        const double lag = s8 / 8.0 * 2.0; // in 1/24-octave bins
        double c = 0;
        int n = 0;
        for (int i = 12; i + 12 < (int) a.size(); ++i) {
            const double j = i + lag;
            if (j < 0 || j >= (double) b.size() - 1) continue;
            const int j0 = (int) j;
            const double f = j - j0, bv = b[(size_t) j0] * (1 - f) + b[(size_t) j0 + 1] * f;
            c += a[(size_t) i] * bv;
            ++n;
        }
        // remove the mean so overall level does not matter
        double ma = 0, mb = 0;
        int m = 0;
        for (int i = 12; i + 12 < (int) a.size(); ++i) {
            const double j = i + lag;
            if (j < 0 || j >= (double) b.size() - 1) continue;
            const int j0 = (int) j;
            const double f = j - j0;
            ma += a[(size_t) i];
            mb += b[(size_t) j0] * (1 - f) + b[(size_t) j0 + 1] * f;
            ++m;
        }
        if (m == 0) continue;
        c = c / n - (ma / m) * (mb / m);
        if (c > bestC) {
            bestC = c;
            best = (float) (s8 / 8.0);
        }
    }
    return best;
}

static adub::VoiceSummary profileOf(const Wav& w, int offset) {
    adub::PitchTracker t;
    adub::VoiceProfile p;
    t.prepare(w.sr);
    p.prepare(w.sr);
    const int hop = (int) std::lround(w.sr * 0.005);
    const auto& x = w.data[0];
    bool voiced = false;
    for (size_t i = (size_t) offset; i < x.size(); ++i) {
        t.push(x[i]);
        p.addAudio(&x[i], 1, voiced);
        if ((i + 1) % (size_t) hop == 0) {
            auto e = t.estimate();
            voiced = e.voiced();
            p.addEstimate(e, hop);
        }
    }
    return p.summary();
}


// A synthetic vowel /a/ (F1 700, F2 1220, F3 2600 Hz) on a glottal pulse train with a little vibrato.
static Wav makeVowel(float f0, int sr) {
    Wav w;
    w.sr = sr;
    w.ch = 1;
    const int n = sr * 3;
    w.data.assign(1, std::vector<float>((size_t) n, 0.0f));
    const float F[3] = {700, 1220, 2600}, B[3] = {90, 110, 160};
    double z[3][2] = {{0, 0}, {0, 0}, {0, 0}}, phase = 0;
    for (int i = 0; i < n; ++i) {
        const double f = f0 * (1.0 + 0.01 * std::sin(2 * M_PI * 5.0 * i / sr));
        phase += f / sr;
        double x = 0;
        if (phase >= 1.0) {
            phase -= 1.0;
            x = 1.0;
        }
        for (int k = 0; k < 3; ++k) {
            const double r = std::exp(-M_PI * B[k] / sr), th = 2 * M_PI * F[k] / sr;
            const double y = x + 2 * r * std::cos(th) * z[k][0] - r * r * z[k][1];
            z[k][1] = z[k][0];
            z[k][0] = y;
            x = y * (1 - r);
        }
        w.data[0][(size_t) i] = (float) x;
    }
    float peak = 0;
    for (float v : w.data[0]) peak = std::max(peak, std::abs(v));
    for (float& v : w.data[0]) v *= 0.5f / peak;
    return w;
}

// Formants by LPC (order 12 on a 10 kHz copy): peaks of the all-pole envelope, middle second.
static std::vector<float> lpcFormants(const Wav& w) {
    const int D = std::max(1, w.sr / 10000), sr = w.sr / D, order = 12;
    std::vector<double> x;
    for (size_t i = (size_t) w.sr; i + D < (size_t) (2 * w.sr) && i < w.data[0].size(); i += (size_t) D) {
        double a = 0;
        for (int k = 0; k < D; ++k) a += w.data[0][i + (size_t) k];
        x.push_back(a / D);
    }
    for (size_t i = x.size() - 1; i > 0; --i) x[i] -= 0.9 * x[i - 1]; // pre-emphasis
    std::vector<double> r((size_t) order + 1, 0.0), a((size_t) order + 1, 0.0), tmp;
    for (int k = 0; k <= order; ++k)
        for (size_t i = (size_t) k; i < x.size(); ++i) r[(size_t) k] += x[i] * x[i - (size_t) k];
    a[0] = 1;
    double err = r[0];
    for (int i = 1; i <= order; ++i) {
        double acc = r[(size_t) i];
        for (int j = 1; j < i; ++j) acc += a[(size_t) j] * r[(size_t) (i - j)];
        const double k = -acc / err;
        tmp = a;
        for (int j = 1; j < i; ++j) a[(size_t) j] = tmp[(size_t) j] + k * tmp[(size_t) (i - j)];
        a[(size_t) i] = k;
        err *= 1 - k * k;
    }
    std::vector<float> peaks;
    double prev2 = 0, prev1 = 0;
    for (int f = 150; f < 4500; f += 2) {
        std::complex<double> A = 0;
        for (int k = 0; k <= order; ++k) A += a[(size_t) k] * std::polar(1.0, -2 * M_PI * f * k / sr);
        const double m = 1.0 / std::abs(A);
        if (f > 154 && prev1 > prev2 && prev1 >= m) peaks.push_back((float) (f - 2));
        prev2 = prev1;
        prev1 = m;
    }
    return peaks;
}

static void print(const char* name, const adub::VoiceSummary& s) {
    std::printf("%-7s F0 median %6.1f Hz (%.1f-%.1f)  F1 %5.0f  F2 %5.0f  F3 %5.0f  centroid %5.0f  breath %.2f  voiced %.1fs\n",
                name, s.medianF0, s.lowF0, s.highF0, s.formants[0], s.formants[1], s.formants[2], s.brightness,
                s.breathiness, s.seconds);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: dubtest in.wav out.wav pitchSemitones formantSemitones\n");
        return 2;
    }
    Wav w;
    const bool synthetic = !std::strcmp(argv[1], "--vowel");
    if (synthetic) w = makeVowel((float) std::atof(argv[2]), 48000);
    else if (!readWav(argv[1], w)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    const int base = synthetic ? 1 : 0; // --vowel takes one extra argument (its pitch)
    if (argc < 5 + base) return 2;
    const char* outPath = argv[2 + base];
    const float ps = (float) std::atof(argv[3 + base]), fs = (float) std::atof(argv[4 + base]);
    adub::PsolaShifter shifter;
    shifter.prepare(w.sr, w.ch);
    shifter.setRatios(std::pow(2.0f, ps / 12.0f), std::pow(2.0f, fs / 12.0f));
    const int lat = shifter.latencySamples();
    Wav o = w;
    for (auto& c : o.data) c.resize(c.size() + (size_t) lat, 0.0f);
    std::vector<float*> ptr((size_t) w.ch);
    const int block = 512;
    std::vector<std::vector<float>> tmp((size_t) w.ch, std::vector<float>(block));
    for (size_t pos = 0; pos < o.data[0].size(); pos += block) {
        const int n = (int) std::min<size_t>(block, o.data[0].size() - pos);
        for (int c = 0; c < w.ch; ++c) {
            for (int i = 0; i < n; ++i)
                tmp[(size_t) c][(size_t) i] = pos + i < w.data[(size_t) c].size() ? w.data[(size_t) c][pos + i] : 0.0f;
            ptr[(size_t) c] = tmp[(size_t) c].data();
        }
        shifter.process(ptr.data(), n);
        for (int c = 0; c < w.ch; ++c)
            for (int i = 0; i < n; ++i) o.data[(size_t) c][pos + i] = tmp[(size_t) c][(size_t) i];
    }
    // remove the latency so the output lines up with the input
    for (auto& c : o.data) c.erase(c.begin(), c.begin() + lat);
    writeWav(outPath, o);

    auto a = profileOf(w, 0), b = profileOf(o, 0);
    print("input", a);
    print("output", b);
    const float f0a = medianF0(w), f0b = medianF0(o);
    const float achieved = f0a > 0 && f0b > 0 ? 12.0f * std::log2(f0b / f0a) : 0.0f;
    std::printf("pitch   wanted %+5.2f st, got %+5.2f st (median %.1f -> %.1f Hz)\n", ps, achieved, f0a, f0b);
    if (synthetic) {
        auto fa = lpcFormants(w), fb = lpcFormants(o);
        for (size_t k = 0; k < std::min<size_t>(3, std::min(fa.size(), fb.size())); ++k)
            std::printf("F%zu      %5.0f -> %5.0f Hz  wanted %+5.2f st, got %+5.2f st\n", k + 1, fa[k], fb[k], fs, 12.0f * std::log2(fb[k] / fa[k]));
    }
    std::printf("formant wanted %+5.2f st, got %+5.2f st (spectral envelope)\n", fs, envelopeShift(envelope(w), envelope(o)));
    // clicks: output sample jumps larger than anything in the input
    double maxIn = 0, rmsIn = 0, rmsOut = 0;
    for (size_t i = 1; i < w.data[0].size(); ++i) maxIn = std::max(maxIn, (double) std::abs(w.data[0][i] - w.data[0][i - 1]));
    int clicks = 0;
    for (size_t i = 1; i < o.data[0].size(); ++i)
        if (std::abs(o.data[0][i] - o.data[0][i - 1]) > maxIn * 1.5) ++clicks;
    for (float v : w.data[0]) rmsIn += v * v;
    for (float v : o.data[0]) rmsOut += v * v;
    std::printf("clicks  %d   level change %+.2f dB   latency %d samples (%.1f ms)\n", clicks,
                10 * std::log10((rmsOut + 1e-12) / (rmsIn + 1e-12)), lat, 1000.0 * lat / w.sr);
    return 0;
}
