// AlptugDUB by Alptuğ Dağ — source-filter cepstral decomposition and anatomical non-linear formant warping.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace adub {

/**
 * Separates the smooth vocal tract envelope (F1..F4 resonances) from glottal harmonics via true
 * liftered cepstral analysis, and warps formants non-linearly according to human vocal tract
 * anatomy (different scaling for pharynx F1, tongue F2, lip/tip F3, and relaxation above 4.2 kHz).
 */
class VocalTractWarper {
public:
    void prepare(double sampleRate) {
        sr = std::max(8000.0, sampleRate);
        fftSize = sr > 120000.0 ? 4096 : sr > 60000.0 ? 2048 : 1024;
        halfBins = fftSize / 2 + 1;
        binHz = (float) (sr / fftSize);

        twiddleFwd.resize((size_t) fftSize / 2);
        twiddleInv.resize((size_t) fftSize / 2);
        for (int k = 0; k < fftSize / 2; ++k) {
            const float ang = (float) (2.0 * M_PI * k / fftSize);
            twiddleFwd[(size_t) k] = {std::cos(ang), -std::sin(ang)};
            twiddleInv[(size_t) k] = {std::cos(ang), std::sin(ang)};
        }

        cepWork.assign((size_t) fftSize, {});
        logEnv.assign((size_t) halfBins, 0.0f);
        logTilt.assign((size_t) halfBins, 0.0f);
        warpedLog.assign((size_t) halfBins, 0.0f);
        smoothLog.assign((size_t) halfBins, 0.0f);
        warpSrcBin.assign((size_t) halfBins, 0.0f);
        fwdMap.assign((size_t) halfBins, 0.0f);
        deltaMap.assign((size_t) halfBins, 0.0f);
        deltaTmp.assign((size_t) halfBins, 0.0f);

        // Precompute log-frequency coordinates for spectral tilt regression (clamped outside 150..6500 Hz to prevent sub-bass blowup)
        logFreq.resize((size_t) halfBins);
        tiltLoBin = std::max(1, (int) std::floor(150.0 / binHz));
        tiltHiBin = std::min(halfBins - 1, (int) std::ceil(6000.0 / binHz));
        for (int k = 0; k < halfBins; ++k) {
            const float f = std::clamp((float) k * binHz, 150.0f, 6500.0f);
            logFreq[(size_t) k] = std::log2(f / 1000.0f);
        }
        lastFormantSt = 999.0f;
        updateWarpTable(0.0f);
    }

    int fftLength() const { return fftSize; }
    int numBins() const { return halfBins; }
    float hzPerBin() const { return binHz; }

    /**
     * Maps a single source formant frequency (Hz) through the anatomical non-linear F1/F2/F3/F4
     * curve for UI visualisation and validation.
     */
    static float warpFormantFreq(float freqHz, float formantSemitones) {
        if (freqHz <= 0.0f || std::abs(formantSemitones) < 1e-4f) return freqHz;
        const float d = std::clamp(formantSemitones, -10.0f, 10.0f);
        const float d1 = d >= 0.0f ? 0.92f * d : 1.20f * d;
        const float d2 = d >= 0.0f ? 1.18f * d : 0.90f * d;
        const float d3 = d >= 0.0f ? 1.05f * d : 0.72f * d;
        const float a1 = std::pow(2.0f, d1 / 12.0f);
        const float a2 = std::pow(2.0f, d2 / 12.0f);
        const float a3 = std::pow(2.0f, d3 / 12.0f);

        // Smooth anatomical landmarks matching the spectral warp table
        const std::array<float, 7> sx = {0.0f, 250.0f, 900.0f, 2600.0f, 3800.0f, 8500.0f, 24000.0f};
        const std::array<float, 7> sy = {
            0.0f,
            250.0f * a1,
            900.0f * std::sqrt(a1 * a2),
            2600.0f * std::sqrt(a2 * a3),
            3800.0f * a3,
            8500.0f,
            24000.0f,
        };
        if (freqHz >= sx.back()) return freqHz;
        for (size_t i = 0; i + 1 < sx.size(); ++i) {
            if (freqHz <= sx[i + 1]) {
                const float t = (freqHz - sx[i]) / (sx[i + 1] - sx[i]);
                return sy[i] + t * (sy[i + 1] - sy[i]);
            }
        }
        return freqHz;
    }

    /**
     * Extracts the true liftered cepstral envelope from a magnitude spectrum `mag[0..halfBins-1]`.
     * Writes the full linear envelope into `outFullEnv` and, if non-null, the zero-tilt vocal
     * tract resonance envelope into `outPureTractEnv` and the slow spectral tilt into `outTiltEnv`.
     */
    void extractEnvelope(const float* mag, float f0, float* outFullEnv, float* outPureTractEnv = nullptr,
                         float* outTiltEnv = nullptr) {
        const float guideF0 = f0 > 45.0f ? f0 : 150.0f;
        const int hiQ = std::max(12, std::min(fftSize / 16, (int) std::lround(0.42 * sr / std::max(60.0f, guideF0))));
        const int maxQ = std::clamp((int) std::lround(0.00125 * sr), 12, hiQ);
        const int taperStart = std::max(4, (maxQ * 3) / 4);

        for (int k = 0; k < halfBins; ++k) {
            const float lm = std::log(std::max(1e-7f, mag[k]));
            cepWork[(size_t) k] = {lm, 0.0f};
            if (k > 0 && k < fftSize / 2) cepWork[(size_t) (fftSize - k)] = {lm, 0.0f};
        }

        fft(cepWork, true);

        // Smooth raised-cosine quefrency lifter to prevent spectral ringing
        for (int q = taperStart; q <= maxQ; ++q) {
            const float u = (float) (q - taperStart) / (float) std::max(1, maxQ - taperStart);
            const float w = 0.5f * (1.0f + std::cos((float) M_PI * u));
            cepWork[(size_t) q] *= w;
            if (q > 0) cepWork[(size_t) (fftSize - q)] *= w;
        }
        for (int q = maxQ + 1; q < fftSize - maxQ; ++q) cepWork[(size_t) q] = {0.0f, 0.0f};

        fft(cepWork, false);

        for (int k = 0; k < halfBins; ++k) {
            const float le = std::clamp(cepWork[(size_t) k].real(), -16.5f, 8.0f);
            logEnv[(size_t) k] = le;
            outFullEnv[k] = std::exp(le);
        }

        if (outPureTractEnv == nullptr && outTiltEnv == nullptr) return;

        // Weighted linear regression in log-frequency vs log-magnitude for vocal tract tilt
        double sx = 0, sy = 0, sxx = 0, sxy = 0, wsum = 0;
        for (int k = tiltLoBin; k <= tiltHiBin; ++k) {
            const float x = logFreq[(size_t) k];
            const float y = logEnv[(size_t) k];
            const float w = 1.0f / (float) std::max(1, k); // equal weight per octave
            sx += w * x;
            sy += w * y;
            sxx += w * x * x;
            sxy += w * x * y;
            wsum += w;
        }
        const double den = wsum * sxx - sx * sx;
        const float slope = den > 1e-9 ? (float) std::clamp((wsum * sxy - sx * sy) / den, -4.5, 2.5) : -1.0f;
        const float intercept = wsum > 0.0 ? (float) ((sy - slope * sx) / wsum) : 0.0f;

        for (int k = 0; k < halfBins; ++k) {
            const float lt = intercept + slope * logFreq[(size_t) k];
            logTilt[(size_t) k] = lt;
            if (outTiltEnv != nullptr) outTiltEnv[k] = std::exp(std::clamp(lt, -16.5f, 8.0f));
            if (outPureTractEnv != nullptr) outPureTractEnv[k] = std::exp(std::clamp(logEnv[(size_t) k] - lt, -3.5f, 3.5f));
        }
    }

    /**
     * Warps a linear spectral envelope `inEnv` non-linearly across F1/F2/F3/F4+ and enhances formant
     * peak contrast by `sharpness` (0..1). Safe to call in-place or to a separate output buffer.
     */
    void warpEnvelope(const float* inEnv, float formantSemitones, float sharpness, float* outEnv) {
        const float clamped = std::clamp(formantSemitones, -10.0f, 10.0f);
        if (std::abs(clamped - lastFormantSt) > 0.01f) updateWarpTable(clamped);

        for (int k = 0; k < halfBins; ++k) {
            const float sb = warpSrcBin[(size_t) k];
            const int i0 = std::clamp((int) sb, 0, halfBins - 2);
            const float frac = std::clamp(sb - (float) i0, 0.0f, 1.0f);
            const float v0 = std::log(std::max(1e-7f, inEnv[i0]));
            const float v1 = std::log(std::max(1e-7f, inEnv[i0 + 1]));
            warpedLog[(size_t) k] = v0 + (v1 - v0) * frac;
        }

        if (sharpness > 0.005f) {
            // Wide ~600 Hz moving average to isolate formant resonance excursions from slow tilt
            const int halfWin = std::max(4, (int) std::lround(300.0 / binHz));
            double runSum = 0;
            // Prefix sum in smoothLog temporarily
            for (int k = 0; k < halfBins; ++k) {
                runSum += warpedLog[(size_t) k];
                smoothLog[(size_t) k] = (float) runSum;
            }
            const float gainBoost = 0.35f * std::clamp(sharpness, 0.0f, 1.2f);
            const int fStart = (int) (180.0f / binHz);
            const int fEnd = std::min(halfBins - 1, (int) (4800.0f / binHz));
            for (int k = 0; k < halfBins; ++k) {
                const int lo = std::max(0, k - halfWin - 1);
                const int hi = std::min(halfBins - 1, k + halfWin);
                const float avg = (smoothLog[(size_t) hi] - (k - halfWin > 0 ? smoothLog[(size_t) lo] : 0.0f)) /
                                  (float) std::max(1, hi - (k - halfWin > 0 ? lo : -1));
                float bandW = 0.0f;
                if (k >= fStart && k <= fEnd) {
                    const float uLo = std::min(1.0f, (float) (k - fStart) / std::max(1.0f, 120.0f / binHz));
                    const float uHi = std::min(1.0f, (float) (fEnd - k) / std::max(1.0f, 600.0f / binHz));
                    bandW = uLo * uHi;
                }
                const float diff = std::clamp(warpedLog[(size_t) k] - avg, -1.6f, 1.6f);
                const float sharpened = avg + diff * (1.0f + gainBoost * bandW);
                outEnv[k] = std::exp(std::clamp(sharpened, -16.5f, 8.0f));
            }
        } else {
            for (int k = 0; k < halfBins; ++k) outEnv[k] = std::exp(std::clamp(warpedLog[(size_t) k], -16.5f, 8.0f));
        }
    }

    /**
     * Converts a positive magnitude gain response `linearGain[0..halfBins-1]` into a causal
     * minimum-phase complex frequency response `outFilter[0..halfBins-1]` via real cepstrum lifting.
     */
    void computeMinimumPhaseFilter(const float* linearGain, std::complex<float>* outFilter) {
        for (int k = 0; k < halfBins; ++k) {
            const float lg = std::log(std::clamp(linearGain[k], 1e-4f, 64.0f));
            cepWork[(size_t) k] = {lg, 0.0f};
            if (k > 0 && k < fftSize / 2) cepWork[(size_t) (fftSize - k)] = {lg, 0.0f};
        }

        fft(cepWork, true);

        cepWork[0] = {cepWork[0].real(), 0.0f};
        for (int q = 1; q < fftSize / 2; ++q) cepWork[(size_t) q] = {2.0f * cepWork[(size_t) q].real(), 0.0f};
        cepWork[(size_t) (fftSize / 2)] = {cepWork[(size_t) (fftSize / 2)].real(), 0.0f};
        for (int q = fftSize / 2 + 1; q < fftSize; ++q) cepWork[(size_t) q] = {0.0f, 0.0f};

        fft(cepWork, false);

        for (int k = 0; k < halfBins; ++k) {
            const float re = std::clamp(cepWork[(size_t) k].real(), -10.0f, 6.0f);
            outFilter[k] = std::polar(std::exp(re), cepWork[(size_t) k].imag());
        }
    }

private:
    void updateWarpTable(float formantSt) {
        lastFormantSt = formantSt;
        const float d1 = formantSt >= 0.0f ? 0.92f * formantSt : 1.20f * formantSt;
        const float d2 = formantSt >= 0.0f ? 1.18f * formantSt : 0.90f * formantSt;
        const float d3 = formantSt >= 0.0f ? 1.05f * formantSt : 0.72f * formantSt;
        const float a1 = std::pow(2.0f, d1 / 12.0f);
        const float a2 = std::pow(2.0f, d2 / 12.0f);
        const float a3 = std::pow(2.0f, d3 / 12.0f);

        const float nyq = (float) (sr * 0.5);
        const float fRelax = std::min(nyq * 0.85f, 8500.0f);
        const std::array<float, 7> sx = {0.0f, 250.0f, 900.0f, 2600.0f, 3800.0f, fRelax, nyq};
        std::array<float, 7> sy = {
            0.0f,
            250.0f * a1,
            900.0f * std::sqrt(a1 * a2),
            2600.0f * std::sqrt(a2 * a3),
            3800.0f * a3,
            fRelax,
            nyq,
        };
        // Guarantee strict monotonicity at any sample rate
        for (size_t i = 1; i < sy.size(); ++i) sy[i] = std::max(sy[i], sy[i - 1] + 20.0f);
        sy.back() = nyq;

        for (int k = 0; k < halfBins; ++k) {
            const float f = (float) k * binHz;
            float y = f;
            for (size_t i = 0; i + 1 < sx.size(); ++i) {
                if (f <= sx[i + 1] || i + 2 == sx.size()) {
                    const float t = std::clamp((f - sx[i]) / std::max(1.0f, sx[i + 1] - sx[i]), 0.0f, 1.0f);
                    y = sy[i] + t * (sy[i + 1] - sy[i]);
                    break;
                }
            }
            fwdMap[(size_t) k] = y / binHz;
        }

        // Smooth the positive frequency increments so band boundaries are C1 smooth
        deltaMap[0] = 0.0f;
        for (int k = 1; k < halfBins; ++k) deltaMap[(size_t) k] = std::max(0.05f, fwdMap[(size_t) k] - fwdMap[(size_t) (k - 1)]);
        const int smoothRadius = std::max(2, (int) std::lround(180.0 / binHz));
        for (int pass = 0; pass < 2; ++pass) {
            for (int k = 1; k < halfBins; ++k) {
                float acc = 0;
                int cnt = 0;
                for (int j = std::max(1, k - smoothRadius); j <= std::min(halfBins - 1, k + smoothRadius); ++j) {
                    acc += deltaMap[(size_t) j];
                    ++cnt;
                }
                deltaTmp[(size_t) k] = acc / (float) std::max(1, cnt);
            }
            std::copy(deltaTmp.begin() + 1, deltaTmp.end(), deltaMap.begin() + 1);
        }
        // Renormalize to span [0 .. halfBins-1] exactly
        double sum = 0;
        for (int k = 1; k < halfBins; ++k) sum += deltaMap[(size_t) k];
        const double scale = sum > 0.0 ? (double) (halfBins - 1) / sum : 1.0;
        fwdMap[0] = 0.0f;
        for (int k = 1; k < halfBins; ++k) fwdMap[(size_t) k] = fwdMap[(size_t) (k - 1)] + (float) (deltaMap[(size_t) k] * scale);

        // Invert monotonic forward map: for each target bin k, find source bin
        int src = 0;
        warpSrcBin[0] = 0.0f;
        for (int k = 1; k < halfBins; ++k) {
            const float target = (float) k;
            while (src + 1 < halfBins - 1 && fwdMap[(size_t) (src + 1)] < target) ++src;
            const float f0 = fwdMap[(size_t) src];
            const float f1 = fwdMap[(size_t) std::min(halfBins - 1, src + 1)];
            const float frac = f1 > f0 + 1e-6f ? std::clamp((target - f0) / (f1 - f0), 0.0f, 1.0f) : 0.0f;
            warpSrcBin[(size_t) k] = (float) src + frac;
        }
    }

    void fft(std::vector<std::complex<float>>& a, bool inverse) const {
        const int n = fftSize;
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[(size_t) i], a[(size_t) j]);
        }
        const auto& tw = inverse ? twiddleInv : twiddleFwd;
        for (int len = 2; len <= n; len <<= 1) {
            const int step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int j = 0; j < len / 2; ++j) {
                    const auto u = a[(size_t) (i + j)];
                    const auto v = a[(size_t) (i + j + len / 2)] * tw[(size_t) (j * step)];
                    a[(size_t) (i + j)] = u + v;
                    a[(size_t) (i + j + len / 2)] = u - v;
                }
            }
        }
        if (inverse) {
            const float invN = 1.0f / (float) n;
            for (auto& x : a) x *= invN;
        }
    }

    double sr = 48000.0;
    int fftSize = 2048, halfBins = 1025;
    float binHz = 23.4375f, lastFormantSt = 999.0f;
    int tiltLoBin = 6, tiltHiBin = 256;
    std::vector<std::complex<float>> twiddleFwd, twiddleInv, cepWork;
    std::vector<float> logEnv, logTilt, logFreq, warpedLog, smoothLog, warpSrcBin, fwdMap, deltaMap, deltaTmp;
};

} // namespace adub
