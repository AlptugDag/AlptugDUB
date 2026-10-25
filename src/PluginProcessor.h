// AlptugDUB by Alptuğ Dağ — the audio processor.
#pragma once

#include "dsp/PsolaShifter.h"
#include "dsp/SpectralShifter.h"
#include "dsp/VoiceProfile.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace ids {
inline constexpr const char* pitch = "pitch";
inline constexpr const char* formant = "formant";
inline constexpr const char* follow = "follow";
inline constexpr const char* body = "body";
inline constexpr const char* air = "air";
inline constexpr const char* mix = "mix";
inline constexpr const char* output = "output";
inline constexpr const char* engine = "engine";
inline constexpr const char* loudness = "loudness";
inline constexpr const char* range = "range";
inline constexpr const char* growl = "growl";
inline constexpr const char* breath = "breath";
inline constexpr const char* age = "age";
inline constexpr const char* nasal = "nasal";
inline constexpr const char* character = "character";
} // namespace ids

class AlptugDUBProcessor : public juce::AudioProcessor {
public:
    AlptugDUBProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AlptugDUB"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState state;

    // ---- read by the editor (lock-free) ----
    std::atomic<float> inputLevel{0}, outputLevel{0}; // RMS, linear
    std::atomic<float> liveF0{0};                     // Hz, 0 when unvoiced
    adub::VoiceSummary voiceSummary() const;
    void resetVoiceProfile() { resetProfile.store(true); }
    /** A character voice: every parameter it needs, computed for this speaker's own voice. */
    struct Character {
        const char* id;
        const char* nameTr;
        const char* nameEn;
        float targetF0;   // Hz the character speaks at (0 = shift relative to the speaker)
        float pitch;      // semitones when relative (or before the voice is learned)
        float formant;    // semitones: vocal tract size
        float range, growl, breath, age, nasal, body, air;
    };
    static const std::vector<Character>& characters();
    /** Applies a character (index into characters()); uses the learned voice when it is ready. */
    void applyCharacter(int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void updateFilters();

    adub::PsolaShifter psola;
    adub::SpectralShifter spectral;
    adub::PitchTracker tracker;
    adub::VoiceProfile profile;
    std::atomic<bool> resetProfile{false};
    mutable juce::SpinLock profileLock;
    adub::VoiceSummary summaryCopy;

    juce::AudioBuffer<float> dry, wide;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay{1 << 16}, padPsola{1 << 16},
        padWide{1 << 16};
    using Eq = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Eq bodyEq, airEq, nasalPeak, nasalLow;
    juce::dsp::IIR::Filter<float> noiseHp, noiseLp; // breath noise, one channel
    juce::AudioBuffer<float> breathNoise;
    float breathEnv = 0, voicedSm = 0;
    juce::Random rng;
    float lastNasal = -1;
    juce::SmoothedValue<float> pitchSm, formantSm, mixSm, gainSm;
    float lastBody = 1000, lastAir = 1000;
    int latency = 0, hop = 240, hopCount = 0;
    double sr = 48000;
};
