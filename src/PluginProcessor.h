// AlptugDUB by Alptuğ Dağ — the audio processor.
#pragma once

#include "dsp/AutoTune.h"
#include "dsp/HybridRegenerator.h"
#include "dsp/NeuralPitch.h"
#include "dsp/SpectralShifter.h"
#include "dsp/VoiceProfile.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
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
inline constexpr const char* autoTract = "autoTract";
inline constexpr const char* autoResponse = "autoResponse";
inline constexpr const char* autoSteady = "autoSteady";
inline constexpr const char* character = "character";
inline constexpr const char* regen = "regen";
inline constexpr const char* glottal = "glottal";
inline constexpr const char* growl = "growl";
inline constexpr const char* breath = "breath";
inline constexpr const char* consonantGuard = "consonantGuard";
inline constexpr const char* autotune = "autotune";
} // namespace ids

class AlptugDUBProcessor : public juce::AudioProcessor,
                           private juce::Timer,
                           private juce::AudioProcessorValueTreeState::Listener {
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
    double getTailLengthSeconds() const override { return 0.35; }

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
    std::atomic<float> liveVoiced{0};                 // 0..1 hybrid engine voiced activity
    std::atomic<float> liveConsonant{0};              // 0..1 consonant guard activity
    std::atomic<float> liveRegen{0};                  // 0..1 active glottal regeneration
    std::atomic<int> liveKey{0};                      // detected musical root key (0=C .. 11=B)
    std::atomic<int> liveScale{2};                    // 0=Major, 1=Minor, 2=Chromatic
    std::atomic<float> liveTuneCents{0};              // active Auto-Tune pitch offset in cents
    std::array<std::atomic<float>, 64> waveHistory{}; // live vocal wave envelope/displacement ring
    std::atomic<int> waveWritePos{0};
    adub::VoiceSummary voiceSummary() const;
    void resetVoiceProfile() { resetProfile.store(true); }
    bool isAutoUpdatingParams() const { return suppressLink.load(std::memory_order_relaxed); }

    /** A character voice, computed for this speaker's own voice. */
    struct Character {
        const char* id;
        const char* nameTr;
        const char* nameEn;
        float targetF0; // Hz the character speaks at (0 = shift relative to the speaker)
        float pitch;    // semitones when relative (or before the voice is learned)
        float formant;  // semitones: vocal tract size
        float age, nasal, body, air;
        float regen, glottal, growl, breath;
    };
    static const std::vector<Character>& characters();
    /** Applies a character (index into characters()); uses the learned voice when it is ready. */
    void applyCharacter(int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void timerCallback() override;
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void updateFilters(float bodyExtra, float airExtra, float presenceExtra);
    float param(const char* id) const { return state.getRawParameterValue(id)->load(); }

    adub::SpectralShifter spectral;
    adub::HybridRegenerator regenerator;
    adub::AutoTune autoTune;
    adub::PitchTracker yin;
    adub::NeuralPitch neural;
    adub::VoiceProfile profile;
    std::atomic<bool> resetProfile{false};
    std::atomic<bool> suppressLink{false}, userTookOver{false};
    std::atomic<bool> hasPendingAuto{false};
    std::atomic<float> pendingAutoPitch{0.0f}, pendingAutoFormant{0.0f};
    float lastAutoSetPitch = 0.0f, lastAutoSetFormant = 0.0f;
    float lastLinkedPitch = 0.0f;
    float fastLogF0 = 0.0f, activeAutoPitch = 0.0f, activeAutoFormant = 0.0f;
    int voicedHopCount = 0;
    mutable juce::SpinLock profileLock;
    adub::VoiceSummary summaryCopy;

    juce::AudioBuffer<float> dry, rawDry, mono;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay{1 << 18};
    using Eq = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Eq bodyEq, airEq, presenceEq, nasalPeak, nasalLow;
    float lastNasal = -1, lastBody = 1000, lastAir = 1000, lastPresence = 1000;
    // live voice dynamics (from the fast tracker, 5 ms): the speaker's melody now and over the last second,
    // and how hard they are speaking against their own average
    float melodyFast = 0, melodySlow = 0, fastF0 = 0, neuralF0 = 0, neuralConf = 0, fastConf = 0;
    float effortFast = 0, effortSlow = 1e-4f, lastPresenceDyn = 0;
    float levelDry = 0, levelWet = 0, levelGain = 1; // keep level
    float limitGain = 1;                             // output safety limiter
    juce::SmoothedValue<float> pitchSm, formantSm, regenSm, glottalSm, growlSm, breathSm, autotuneSm, mixSm, gainSm;
    int latency = 0, hop = 240, hopCount = 0;
    double sr = 48000;
};
