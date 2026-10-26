// AlptugDUB by Alptuğ Dağ — the audio processor.
#pragma once

#include "dsp/NeuralPitch.h"
#include "dsp/SpectralShifter.h"
#include "dsp/VoiceProfile.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace ids {
inline constexpr const char* pitch = "pitch";
inline constexpr const char* formant = "formant";
inline constexpr const char* follow = "follow";
inline constexpr const char* age = "age";
inline constexpr const char* nasal = "nasal";
inline constexpr const char* presence = "presence";
inline constexpr const char* body = "body";
inline constexpr const char* air = "air";
inline constexpr const char* mix = "mix";
inline constexpr const char* output = "output";
inline constexpr const char* loudness = "loudness";
inline constexpr const char* ai = "ai";
inline constexpr const char* autoMatch = "autoMatch";
inline constexpr const char* autoTone = "autoTone";
inline constexpr const char* character = "character";
} // namespace ids

class AlptugDUBProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    AlptugDUBProcessor();
    ~AlptugDUBProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AlptugDUB"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState state;

    // ---- read by the editor (lock-free) ----
    std::atomic<float> inputLevel{0}, outputLevel{0}; // peak, linear
    std::atomic<float> liveF0{0};                     // Hz, 0 when unvoiced
    std::atomic<float> liveConfidence{0};             // the neural tracker's voicing probability
    adub::VoiceSummary voiceSummary() const;
    void resetVoiceProfile() { resetProfile.store(true); }

    /** A character voice, computed for this speaker's own voice. */
    struct Character {
        const char* id;
        const char* nameTr;
        const char* nameEn;
        float targetF0; // Hz the character speaks at (0 = shift relative to the speaker)
        float pitch;    // semitones when relative (or before the voice is learned)
        float formant;  // semitones: vocal tract size
        float age, nasal, body, air;
    };
    static const std::vector<Character>& characters();
    /** Applies a character (index into characters()); uses the learned voice when it is ready. */
    void applyCharacter(int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void timerCallback() override;
    void updateFilters(float bodyExtra, float airExtra);
    float param(const char* id) const { return state.getRawParameterValue(id)->load(); }

    adub::SpectralShifter spectral;
    adub::PitchTracker yin;
    adub::NeuralPitch neural;
    adub::VoiceProfile profile;
    std::atomic<bool> resetProfile{false};
    float autoPitch = 1000, autoFormant = 1000; // what auto-match last wrote (1000: nothing yet)
    mutable juce::SpinLock profileLock;
    adub::VoiceSummary summaryCopy;

    juce::AudioBuffer<float> dry, mono;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay{1 << 17};
    using Eq = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Eq bodyEq, airEq, presenceEq, nasalPeak, nasalLow;
    float lastNasal = -1, lastBody = 1000, lastAir = 1000, lastPresence = 1000;
    float levelDry = 0, levelWet = 0, levelGain = 1; // keep level
    float limitGain = 1;                             // output safety limiter
    juce::SmoothedValue<float> pitchSm, formantSm, mixSm, gainSm;
    int latency = 0, hop = 240, hopCount = 0;
    double sr = 48000;
};
