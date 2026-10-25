// AlptugDUB by Alptuğ Dağ — the "wide" engine for material that is not a single voice.
//
// Signalsmith Stretch (MIT, signalsmith-audio.co.uk) shifts pitch and formants spectrally. It handles
// choirs, voices over music and noisy takes, where PSOLA has no single pitch to follow; for one clean
// voice the PSOLA engine sounds more natural.
#pragma once

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <vector>

namespace adub {

class SpectralShifter {
public:
    void prepare(double sampleRate, int numChannels, int maxBlock) {
        sr = sampleRate;
        channels = numChannels;
        stretch.presetDefault(numChannels, (float) sampleRate);
        scratch.assign((size_t) numChannels, std::vector<float>((size_t) maxBlock, 0.0f));
        stretch.reset();
    }

    int latencySamples() const { return stretch.inputLatency() + stretch.outputLatency(); }

    /** Semitones; the voice's middle pitch (Hz) guides formant handling. */
    void set(float pitchSemitones, float formantSemitones, float baseF0) {
        stretch.setTransposeSemitones(pitchSemitones);
        stretch.setFormantSemitones(formantSemitones, true);
        stretch.setFormantBase((baseF0 > 0 ? baseF0 : 150.0f) / (float) sr);
    }

    void process(float* const* data, int n) {
        if ((int) scratch[0].size() < n)
            for (auto& c : scratch) c.resize((size_t) n);
        for (int c = 0; c < channels; ++c) std::copy(data[c], data[c] + n, scratch[(size_t) c].begin());
        std::vector<float*> in((size_t) channels);
        for (int c = 0; c < channels; ++c) in[(size_t) c] = scratch[(size_t) c].data();
        stretch.process(in.data(), n, data, n);
    }

    void reset() { stretch.reset(); }

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    std::vector<std::vector<float>> scratch;
    double sr = 48000;
    int channels = 2;
};

} // namespace adub
