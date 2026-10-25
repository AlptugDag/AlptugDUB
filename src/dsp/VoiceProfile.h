// AlptugDUB by Alptuğ Dağ — the character of the incoming voice.
//
// Learns, from the voiced parts only: the pitch distribution (median, range, voice type), the first
// three formants (peaks of a cepstrally smoothed spectral envelope), brightness (spectral centroid)
// and breathiness (YIN aperiodicity). From that it suggests the pitch and formant shift that turn
// this voice into a chosen target, keeping the change as small as the target allows.
#pragma once

#include "PitchTracker.h"

#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace adub {

enum class VoiceType { Unknown, Bass, Baritone, Tenor, Alto, MezzoSoprano, Soprano, Child };

/** Typical speaking pitch (Hz) of each voice type, used as targets. */
inline float typicalF0(VoiceType t) {
    switch (t) {
        case VoiceType::Bass: return 92.0f;
        case VoiceType::Baritone: return 112.0f;
        case VoiceType::Tenor: return 140.0f;
        case VoiceType::Alto: return 185.0f;
        case VoiceType::MezzoSoprano: return 212.0f;
        case VoiceType::Soprano: return 245.0f;
        case VoiceType::Child: return 285.0f;
        default: return 0.0f;
    }
}

inline VoiceType classify(float f0) {
    if (f0 <= 0) return VoiceType::Unknown;
    if (f0 < 102) return VoiceType::Bass;
    if (f0 < 126) return VoiceType::Baritone;
    if (f0 < 162) return VoiceType::Tenor;
    if (f0 < 198) return VoiceType::Alto;
    if (f0 < 228) return VoiceType::MezzoSoprano;
    if (f0 < 265) return VoiceType::Soprano;
    return VoiceType::Child;
}

struct VoiceSummary {
    float medianF0 = 0, lowF0 = 0, highF0 = 0; // median, 10th and 90th percentile
    std::array<float, 3> formants{};           // F1..F3 in Hz
    float brightness = 0;                      // spectral centroid of voiced frames, Hz
    float breathiness = 0;                     // 0 clear .. 1 breathy
    float seconds = 0;                         // voiced audio learned
    VoiceType type = VoiceType::Unknown;
    bool ready() const { return seconds >= 2.0f && medianF0 > 0; }
};

class VoiceProfile {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        frame = sampleRate > 60000 ? 4096 : 2048;
        buf.assign((size_t) frame, 0.0f);
        fill = 0;
        hannW.resize((size_t) frame);
        for (int i = 0; i < frame; ++i) hannW[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * (float) M_PI * i / (frame - 1));
        reset();
    }

    void reset() {
        hist.fill(0.0f);
        total = 0;
        formantAcc.fill(0.0f);
        formantN = 0;
        centroidAcc = 0;
        breathAcc = 0;
        frames = 0;
        voicedSamples = 0;
    }

    /** Feeds the input voice and the tracker's newest estimate (call once per hop). */
    void addEstimate(const PitchEstimate& e, int hopSamples) {
        if (!e.voiced() || e.rms < 0.003f) return;
        const float f0 = (float) (sr / e.period);
        const int bin = binOf(f0);
        if (bin >= 0) {
            hist[(size_t) bin] += 1.0f;
            total += 1.0f;
        }
        breathAcc += e.aperiodicity / 0.15f;
        ++frames;
        voicedSamples += hopSamples;
        lastVoiced = true;
    }

    /** Feeds raw input (mono) for the spectral measures. */
    void addAudio(const float* x, int n, bool voicedNow) {
        for (int i = 0; i < n; ++i) {
            buf[(size_t) fill++] = x[i];
            if (fill == frame) {
                if (voicedNow) analyseFrame();
                // 50% overlap
                std::copy(buf.begin() + frame / 2, buf.end(), buf.begin());
                fill = frame / 2;
            }
        }
    }

    VoiceSummary summary() const {
        VoiceSummary s;
        s.seconds = (float) (voicedSamples / sr);
        if (total > 0) {
            s.medianF0 = percentile(0.5f);
            s.lowF0 = percentile(0.1f);
            s.highF0 = percentile(0.9f);
            s.type = classify(s.medianF0);
        }
        if (formantN > 0)
            for (int k = 0; k < 3; ++k) s.formants[(size_t) k] = formantAcc[(size_t) k] / (float) formantN;
        if (formantN > 0) s.brightness = centroidAcc / (float) formantN;
        if (frames > 0) s.breathiness = std::clamp(breathAcc / (float) frames, 0.0f, 1.0f);
        return s;
    }

    /**
     * Semitones of pitch and formant shift that move this voice to the target type. Formants follow
     * the pitch by about a quarter (adult vocal tracts differ far less than voices do).
     */
    static std::pair<float, float> suggestion(const VoiceSummary& s, VoiceType target) {
        if (!s.ready() || target == VoiceType::Unknown) return {0.0f, 0.0f};
        float pitch = 12.0f * std::log2(typicalF0(target) / s.medianF0);
        pitch = std::clamp(pitch, -12.0f, 12.0f);
        float formant = std::clamp(pitch * 0.25f, -6.0f, 6.0f);
        if (target == VoiceType::Child) formant = std::clamp(formant + 1.5f, -6.0f, 6.0f);
        return {pitch, formant};
    }

private:
    static constexpr int kBins = 72; // 6 octaves from 40 Hz, 12 per octave
    static int binOf(float f0) {
        if (f0 < 40.0f) return -1;
        int b = (int) std::floor(12.0f * std::log2(f0 / 40.0f));
        return b >= 0 && b < kBins ? b : -1;
    }

    float percentile(float q) const {
        float target = total * q, acc = 0;
        for (int b = 0; b < kBins; ++b) {
            acc += hist[(size_t) b];
            if (acc >= target) return 40.0f * std::pow(2.0f, (b + 0.5f) / 12.0f);
        }
        return 0;
    }

    static void fft(std::vector<std::complex<float>>& a, bool inverse) {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1) {
            const float ang = 2.0f * (float) M_PI / (float) len * (inverse ? 1.0f : -1.0f);
            const std::complex<float> wl(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len) {
                std::complex<float> w(1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j) {
                    auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse)
            for (auto& x : a) x /= (float) n;
    }

    void analyseFrame() {
        std::vector<std::complex<float>> spec((size_t) frame);
        double energy = 0;
        for (int i = 0; i < frame; ++i) {
            spec[(size_t) i] = buf[(size_t) i] * hannW[(size_t) i];
            energy += buf[(size_t) i] * buf[(size_t) i];
        }
        if (energy / frame < 1e-5) return;
        fft(spec, false);
        const int half = frame / 2;
        std::vector<float> mag((size_t) half);
        double num = 0, den = 0;
        for (int k = 0; k < half; ++k) {
            const float m = std::abs(spec[(size_t) k]);
            mag[(size_t) k] = m;
            const double f = (double) k * sr / frame;
            num += f * m;
            den += m;
        }
        // real cepstrum, liftered below 1.6 ms: the vocal tract's envelope, without the harmonics
        std::vector<std::complex<float>> cep((size_t) frame);
        for (int k = 0; k < frame; ++k) {
            const int kk = k < half ? k : frame - k;
            cep[(size_t) k] = std::log(std::max(1e-9f, kk < half ? mag[(size_t) kk] : mag[(size_t) half - 1]));
        }
        fft(cep, true);
        const int lifter = std::max(8, (int) (0.0016 * sr));
        for (int q = lifter; q <= frame - lifter; ++q) cep[(size_t) q] = 0;
        fft(cep, false);
        std::array<float, 3> found{};
        int nf = 0;
        const int lo = (int) (200.0 * frame / sr), hi = (int) (3800.0 * frame / sr);
        for (int k = lo + 1; k < hi && nf < 3; ++k) {
            float a = cep[(size_t) k - 1].real(), b = cep[(size_t) k].real(), c = cep[(size_t) k + 1].real();
            if (b > a && b >= c) {
                const float denom = a - 2 * b + c;
                const float off = std::abs(denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
                found[(size_t) nf++] = (float) ((k + off) * sr / frame);
            }
        }
        if (nf == 3) {
            for (int i = 0; i < 3; ++i) formantAcc[(size_t) i] += found[(size_t) i];
            centroidAcc += (float) (num / std::max(1e-9, den));
            ++formantN;
        }
    }

    double sr = 48000;
    int frame = 2048, fill = 0;
    std::vector<float> buf, hannW;
    std::array<float, kBins> hist{};
    float total = 0;
    std::array<float, 3> formantAcc{};
    int formantN = 0;
    float centroidAcc = 0, breathAcc = 0;
    int frames = 0;
    double voicedSamples = 0;
    bool lastVoiced = false;
};

} // namespace adub
