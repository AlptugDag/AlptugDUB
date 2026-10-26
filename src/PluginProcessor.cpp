// AlptugDUB by Alptuğ Dağ — the audio processor.
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
float semis(float s) { return std::pow(2.0f, s / 12.0f); }

/** The speaker's vocal tract, as semitones above an adult man's, guessed from the middle pitch. */
float speakerTract(float medianF0) {
    if (medianF0 <= 0) return 0.0f;
    if (medianF0 < 165) return 0.0f;
    if (medianF0 < 255) return 2.2f;
    return 4.0f;
}

/**
 * Age, -50 (younger) .. +50 (older). Younger voices sit a little higher with a shorter tract, are
 * steadier and brighter; older ones tremble, waver in level, breathe more and darken slightly.
 */
struct AgeShape {
    float pitch = 0, formant = 0, stability = 0, air = 0, breath = 0, wear = 0;
};
AgeShape ageShape(float age) {
    const float a = juce::jlimit(-1.0f, 1.0f, age / 50.0f);
    AgeShape s;
    if (a < 0) {
        const float y = -a;
        s.pitch = 1.0f * y;
        s.formant = 1.2f * y;
        s.stability = 0.15f * y;
        s.air = 1.5f * y;
    } else {
        s.wear = a;
        s.breath = 0.12f * a;
        s.formant = -0.3f * a;
    }
    return s;
}
} // namespace

const std::vector<AlptugDUBProcessor::Character>& AlptugDUBProcessor::characters() {
    // targetF0: the character's speaking pitch. formant: its vocal tract, in semitones above an adult man's.
    static const std::vector<Character> list = {
        //  id          Türkçe          English        F0   pitch tract range growl breath age  nasal body  air  resp tract stab
        {"natural",   "Doğal",        "Natural",       0,   0,   0.0f,  1.00f, 0.00f, 0.00f,   0, 0.00f, 0.0f,  0.0f, 0.5f, 0.3f, 0.0f},
        {"bigman",    "İri adam",     "Big man",      88,  -4,  -2.5f,  0.85f, 0.08f, 0.00f,   5, 0.00f, 3.0f, -1.0f, 0.5f, 0.3f, 0.1f},
        {"giant",     "Dev",          "Giant",        70,  -8,  -5.0f,  0.70f, 0.20f, 0.05f,  10, 0.00f, 4.0f, -2.0f, 0.6f, 0.2f, 0.3f},
        {"monster",   "Canavar",      "Monster",      62, -10,  -4.0f,  0.55f, 0.75f, 0.15f,  15, 0.00f, 4.0f, -1.0f, 0.9f, 0.1f, 0.4f},
        {"man",       "Erkek",        "Man",         115,  -7,   0.0f,  0.95f, 0.00f, 0.00f,   0, 0.00f, 1.5f,  0.0f, 0.5f, 0.3f, 0.0f},
        {"woman",     "Kadın",        "Woman",       205,   7,   2.2f,  1.10f, 0.00f, 0.08f,   0, 0.00f, 0.0f,  0.5f, 0.4f, 0.35f, 0.0f},
        {"girl",      "Küçük kız",    "Little girl", 300,   9,   4.5f,  1.30f, 0.00f, 0.12f, -40, 0.05f,-2.0f,  0.5f, 0.3f, 0.4f, 0.0f},
        {"boy",       "Küçük oğlan",  "Little boy",  265,   7,   4.0f,  1.25f, 0.00f, 0.08f, -35, 0.00f,-1.5f,  0.5f, 0.3f, 0.4f, 0.0f},
        {"oldman",    "Yaşlı adam",   "Old man",     125,  -1,   0.0f,  0.90f, 0.05f, 0.30f,  30, 0.10f, 0.0f, -1.0f, 0.6f, 0.2f, 0.0f},
        {"oldwoman",  "Yaşlı kadın",  "Old woman",   185,   4,   2.0f,  1.00f, 0.00f, 0.30f,  30, 0.15f, 0.0f, -1.0f, 0.6f, 0.2f, 0.0f},
        {"witch",     "Cadı",         "Witch",       235,   5,   2.6f,  1.45f, 0.12f, 0.35f,  25, 0.60f,-2.0f,  0.5f, 0.7f, 0.3f, 0.0f},
        {"dwarf",     "Cüce",         "Dwarf",       120,  -2,   2.5f,  0.90f, 0.15f, 0.00f,   5, 0.20f, 1.0f,  0.0f, 0.6f, 0.3f, 0.1f},
        {"goblin",    "Goblin",       "Goblin",      270,   6,   3.5f,  1.35f, 0.35f, 0.10f,   5, 0.55f,-2.0f,  0.0f, 0.8f, 0.3f, 0.0f},
    };
    return list;
}

juce::AudioProcessorValueTreeState::ParameterLayout AlptugDUBProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto st = [](float v, int) { return (v > 0.004f ? "+" : "") + String(v, 1) + " st"; };
    auto pct = [](float v, int) { return String(roundToInt(v * 100)) + "%"; };
    auto db = [](float v, int) { return (v > 0.04f ? "+" : "") + String(v, 1) + " dB"; };
    auto years = [](float v, int) { return v > 0.5f ? "+" + String(roundToInt(v)) : String(roundToInt(v)); };
    auto addFloat = [&](const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String(float, int)> text) {
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction(text)));
    };
    addFloat(ids::pitch, "Pitch", {-18, 18, 0.01f}, 0.0f, st);
    addFloat(ids::formant, "Formant", {-8, 8, 0.01f}, 0.0f, st);
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::follow, 1}, "Formant follows pitch", false));
    addFloat(ids::range, "Intonation", {0.3f, 2.0f, 0.01f}, 1.0f, pct);
    addFloat(ids::age, "Age", {-50, 50, 1}, 0.0f, years);
    addFloat(ids::growl, "Growl", {0, 1, 0.01f}, 0.0f, pct);
    addFloat(ids::breath, "Breath", {0, 1, 0.01f}, 0.0f, pct);
    addFloat(ids::nasal, "Nasal", {0, 1, 0.01f}, 0.0f, pct);
    addFloat(ids::response, "Response", {0, 1, 0.01f}, 0.5f, pct);
    addFloat(ids::tract, "Tract motion", {0, 1, 0.01f}, 0.3f, pct);
    addFloat(ids::stability, "Stability", {0, 1, 0.01f}, 0.0f, pct);
    addFloat(ids::presence, "Presence", {-6, 6, 0.1f}, 0.0f, db);
    addFloat(ids::body, "Body", {-6, 6, 0.1f}, 0.0f, db);
    addFloat(ids::air, "Air", {-6, 6, 0.1f}, 0.0f, db);
    addFloat(ids::mix, "Mix", {0, 1, 0.01f}, 1.0f, pct);
    addFloat(ids::output, "Output", {-12, 12, 0.1f}, 0.0f, db);
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ids::engine, 1}, "Engine", StringArray{"Natural voice", "Wide"}, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::loudness, 1}, "Keep loudness", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::ai, 1}, "AI analysis", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoMatch, 1}, "Auto match character", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoTract, 1}, "Natural tract motion", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoResponse, 1}, "Dynamic response", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoTone, 1}, "Smart tone", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoSteady, 1}, "Steady pitch", false));
    StringArray names;
    for (auto& c : characters()) names.add(c.nameEn);
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ids::character, 1}, "Character", names, 0));
    return l;
}

AlptugDUBProcessor::AlptugDUBProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "AlptugDUB", layout()) {
    state.addParameterListener(ids::ai, this);
    wantNeural.store(param(ids::ai) > 0.5f);
    startTimerHz(2);
}

AlptugDUBProcessor::~AlptugDUBProcessor() {
    stopTimer();
    state.removeParameterListener(ids::ai, this);
    cancelPendingUpdate();
}

bool AlptugDUBProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

int AlptugDUBProcessor::totalLatency(bool useNeural) const {
    const int p = useNeural ? psola.latencyForAnalysis(neural.delaySamples(), neural.spacing())
                            : psola.latencyForAnalysis(yin.delaySamples(), hop);
    return std::max(p, spectral.latencySamples());
}

void AlptugDUBProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sr = sampleRate;
    const int ch = std::max(1, getTotalNumOutputChannels());
    yin.prepare(sampleRate);
    neural.prepare(sampleRate);
    psola.prepare(sampleRate, ch, std::max<int64_t>(neural.delaySamples(), yin.delaySamples()));
    spectral.prepare(sampleRate, ch, samplesPerBlock);
    profile.prepare(sampleRate);
    hop = std::max(1, (int) std::lround(sampleRate * 0.005));

    juce::dsp::ProcessSpec spec{sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) ch};
    for (auto* d : {&dryDelay, &padPsola, &padWide}) d->prepare(spec);
    dry.setSize(ch, samplesPerBlock);
    mono.setSize(1, samplesPerBlock);
    for (auto* f : {&bodyEq, &airEq, &presenceEq, &nasalPeak, &nasalLow}) {
        f->prepare(spec);
        f->reset();
    }
    noiseHp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 900.0, 0.7);
    noiseLp.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, std::min(4800.0, sampleRate * 0.3), 0.7);
    lastBody = lastAir = lastNasal = lastPresence = 1000;
    for (auto* s : {&pitchSm, &formantSm, &mixSm, &gainSm}) s->reset(sampleRate, 0.05);

    neuralActive = wantNeural.load();
    wideActive = param(ids::engine) > 0.5f;
    configureAnalysis();
    setLatencySamples(latency);
    reset();
}

void AlptugDUBProcessor::configureAnalysis() {
    // A change of analysis restarts every stream, so their sample clocks agree again.
    if (neuralActive) psola.setAnalysis(neural.delaySamples(), neural.spacing());
    else psola.setAnalysis(yin.delaySamples(), hop);
    yin.reset();
    neural.reset();
    spectral.reset();
    hopCount = 0;
    levelDry = levelWet = 0;
    levelGain = 1;
    latency = totalLatency(neuralActive);
    dryDelay.setDelay((float) latency);
    padPsola.setDelay((float) (latency - psola.latencySamples()));
    padWide.setDelay((float) (latency - spectral.latencySamples()));
}

void AlptugDUBProcessor::reset() {
    if (mono.getNumSamples() == 0) return; // not prepared yet
    configureAnalysis();
    for (auto* d : {&dryDelay, &padPsola, &padWide}) d->reset();
    noiseHp.reset();
    noiseLp.reset();
    breathEnv = lowEnv = lowA = lowB = 0;
    levelDry = levelWet = 0;
    levelGain = 1;
    limitGain = 1;
    pitchSm.setCurrentAndTargetValue(param(ids::pitch));
    formantSm.setCurrentAndTargetValue(param(ids::formant));
    mixSm.setCurrentAndTargetValue(param(ids::mix));
    gainSm.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(param(ids::output)));
}

void AlptugDUBProcessor::parameterChanged(const juce::String&, float) { triggerAsyncUpdate(); }

void AlptugDUBProcessor::handleAsyncUpdate() {
    // Tell the host the new delay first; the audio thread switches on its next block.
    const bool want = param(ids::ai) > 0.5f;
    setLatencySamples(totalLatency(want));
    wantNeural.store(want);
}

void AlptugDUBProcessor::updateFilters(float bodyExtra, float airExtra) {
    const float body = juce::jlimit(-9.0f, 9.0f, param(ids::body) + bodyExtra);
    const float air = juce::jlimit(-9.0f, 9.0f, param(ids::air) + airExtra);
    const float nasal = param(ids::nasal);
    const float presence = param(ids::presence);
    if (std::abs(body - lastBody) > 0.01f) {
        *bodyEq.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, 170.0, 0.7, juce::Decibels::decibelsToGain(body));
        lastBody = body;
    }
    if (std::abs(air - lastAir) > 0.01f) {
        *airEq.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, std::min(9000.0, sr * 0.4), 0.7, juce::Decibels::decibelsToGain(air));
        lastAir = air;
    }
    if (presence != lastPresence) {
        *presenceEq.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 3200.0, 0.9, juce::Decibels::decibelsToGain(presence));
        lastPresence = presence;
    }
    if (nasal != lastNasal) {
        // nasality: the honk of a nasal resonance near 1.1 kHz, less chest around 300 Hz
        *nasalPeak.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 1150.0, 2.2, juce::Decibels::decibelsToGain(8.0f * nasal));
        *nasalLow.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, 300.0, 1.0, juce::Decibels::decibelsToGain(-4.0f * nasal));
        lastNasal = nasal;
    }
}

adub::VoiceSummary AlptugDUBProcessor::voiceSummary() const {
    const juce::SpinLock::ScopedLockType lock(profileLock);
    return summaryCopy;
}

void AlptugDUBProcessor::applyCharacter(int index) {
    const auto& list = characters();
    if (index < 0 || index >= (int) list.size()) return;
    const Character& c = list[(size_t) index];
    const auto s = voiceSummary();
    float pitch = c.pitch, formant = c.formant;
    if (c.targetF0 > 0 && s.ready()) {
        pitch = 12.0f * std::log2(c.targetF0 / s.medianF0);
        formant = c.formant - speakerTract(s.medianF0);
    }
    // age moves pitch and tract by itself; take that out so the character lands where it should
    const auto shape = ageShape(c.age);
    pitch -= shape.pitch;
    formant -= shape.formant;
    if (index == 0) pitch = formant = 0;
    auto set = [&](const char* id, float v) {
        if (auto* p = state.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    set(ids::pitch, juce::jlimit(-18.0f, 18.0f, pitch));
    set(ids::formant, juce::jlimit(-8.0f, 8.0f, formant));
    autoPitch = param(ids::pitch);
    autoFormant = param(ids::formant);
    set(ids::follow, 0.0f);
    set(ids::range, c.range);
    set(ids::growl, c.growl);
    set(ids::breath, c.breath);
    set(ids::age, c.age);
    set(ids::nasal, c.nasal);
    set(ids::body, c.body);
    set(ids::air, c.air);
    set(ids::response, c.response);
    set(ids::tract, c.tract);
    set(ids::stability, c.stability);
    set(ids::character, (float) index);
}

void AlptugDUBProcessor::timerCallback() {
    // Auto-match: while the voice is being learned (or changes), keep the chosen character on its
    // target pitch. Moving the knob by hand takes over until the next character is picked.
    if (param(ids::autoMatch) < 0.5f) return;
    const int index = juce::roundToInt(param(ids::character));
    const auto& list = characters();
    if (index <= 0 || index >= (int) list.size()) return;
    const Character& c = list[(size_t) index];
    const auto s = voiceSummary();
    if (c.targetF0 <= 0 || !s.ready()) return;
    if (std::abs(param(ids::pitch) - autoPitch) > 0.02f || std::abs(param(ids::formant) - autoFormant) > 0.02f) return; // taken over
    const auto shape = ageShape(c.age);
    const float pitch = juce::jlimit(-18.0f, 18.0f, 12.0f * std::log2(c.targetF0 / s.medianF0) - shape.pitch);
    const float formant = juce::jlimit(-8.0f, 8.0f, c.formant - speakerTract(s.medianF0) - shape.formant);
    auto set = [&](const char* id, float v) {
        if (auto* p = state.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    if (std::abs(pitch - autoPitch) > 0.2f) set(ids::pitch, pitch);
    if (std::abs(formant - autoFormant) > 0.2f) set(ids::formant, formant);
    autoPitch = param(ids::pitch);
    autoFormant = param(ids::formant);
}

void AlptugDUBProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), ch = buffer.getNumChannels();
    if (n == 0 || ch == 0) return;
    if (dry.getNumSamples() < n || dry.getNumChannels() < ch) {
        dry.setSize(ch, n, false, false, true); // only if the host breaks its promised block size
        mono.setSize(1, n, false, false, true);
    }

    // ---- switch analysis or engine when asked: every stream restarts together ----
    const bool wide = param(ids::engine) > 0.5f;
    if (wantNeural.load() != neuralActive || wide != wideActive) {
        neuralActive = wantNeural.load();
        wideActive = wide;
        configureAnalysis();
    }

    // ---- listen: neural pitch (the AI), YIN (fast, and breathiness), the voice profile ----
    if (resetProfile.exchange(false)) profile.reset();
    float inPeak = 0;
    bool voicedNow = false;
    auto* m = mono.getWritePointer(0);
    for (int i = 0; i < n; ++i) {
        float s = 0;
        for (int c = 0; c < ch; ++c) s += buffer.getSample(c, i);
        s /= (float) ch;
        m[i] = s;
        inPeak = std::max(inPeak, std::abs(s));
        yin.push(s);
        if (++hopCount >= hop) {
            hopCount = 0;
            const auto e = yin.estimate();
            if (e.voiced()) profile.addBreath(e.aperiodicity);
            voicedNow = e.voiced() && e.rms > 0.003f;
            if (!neuralActive) {
                psola.pushEstimate(e);
                liveF0.store(voicedNow ? (float) (sr / e.period) : 0.0f);
            }
        }
        if (neural.push(s)) {
            const auto& e = neural.latest();
            profile.addEstimate(e, neural.spacing());
            if (neuralActive) {
                psola.pushEstimate(e);
                liveF0.store(e.voiced() ? (float) (sr / e.period) : 0.0f);
            }
            liveConfidence.store(e.confidence);
        }
    }
    profile.addAudio(m, n, voicedNow);
    {
        const juce::SpinLock::ScopedTryLockType lock(profileLock);
        if (lock.isLocked()) summaryCopy = profile.summary();
    }
    inputLevel.store(inPeak);

    // ---- dry copy, aligned with the processed signal ----
    for (int c = 0; c < ch; ++c) dry.copyFrom(c, 0, buffer, c, 0, n);
    {
        juce::dsp::AudioBlock<float> b(dry.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        dryDelay.process(juce::dsp::ProcessContextReplacing<float>(b));
    }

    // ---- the voice engine ----
    const auto shape = ageShape(param(ids::age));
    pitchSm.setTargetValue(param(ids::pitch) + shape.pitch);
    formantSm.setTargetValue(param(ids::formant) + shape.formant);
    pitchSm.skip(n);
    formantSm.skip(n);
    const float p = pitchSm.getCurrentValue();
    float f = formantSm.getCurrentValue();
    if (param(ids::follow) > 0.5f) f += 0.25f * p;
    const float median = summaryCopy.medianF0;
    juce::dsp::AudioBlock<float> wet(buffer);
    if (!wideActive) {
        psola.setRatios(semis(p), semis(f));
        psola.setCharacter(param(ids::range), param(ids::growl), shape.wear, median);
        const float response = param(ids::autoResponse) > 0.5f ? param(ids::response) : 0.0f;
        const float tract = param(ids::autoTract) > 0.5f ? param(ids::tract) : 0.0f;
        const float steady = param(ids::autoSteady) > 0.5f ? 0.35f : 0.0f;
        psola.setDynamics(response, tract, juce::jlimit(0.0f, 1.0f, param(ids::stability) + shape.stability + steady));
        psola.setLoudnessMatch(false); // matched at the very end, after tone and breath
        psola.process(buffer.getArrayOfWritePointers(), n);
        padPsola.process(juce::dsp::ProcessContextReplacing<float>(wet));
    } else {
        spectral.set(p, f, median);
        spectral.process(buffer.getArrayOfWritePointers(), n);
        padWide.process(juce::dsp::ProcessContextReplacing<float>(wet));
    }

    // ---- breath: aspiration noise, only where the voice is voiced ----
    // Real breathiness is noise inside the voiced sound, shaped like the voice and following its
    // pulses. It is added only where low frequencies dominate the finished signal (vowels, not "s",
    // not silence), at a level that stays well under the voice, so it never turns into hiss.
    const float breath = juce::jlimit(0.0f, 1.0f, param(ids::breath) + shape.breath);
    if (breath > 0.001f) {
        const float response = param(ids::autoResponse) > 0.5f ? param(ids::response) : 0.0f;
        const float att = 1.0f - std::exp(-1.0f / (0.003f * (float) sr)), rel = 1.0f - std::exp(-1.0f / (0.060f * (float) sr));
        const float slow = 1.0f - std::exp(-1.0f / (1.5f * (float) sr));
        const float lowCoef = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 700.0f / (float) sr);
        for (int i = 0; i < n; ++i) {
            const float nz = noiseLp.processSample(noiseHp.processSample(rng.nextFloat() * 2.0f - 1.0f));
            float m = 0;
            for (int c = 0; c < ch; ++c) m += buffer.getSample(c, i);
            m /= (float) ch;
            lowA += (m - lowA) * lowCoef;
            lowB += (lowA - lowB) * lowCoef;
            const float level = std::abs(m), lowLevel = std::abs(lowB);
            breathEnv += (level - breathEnv) * (level > breathEnv ? att : rel);
            lowEnv += (lowLevel - lowEnv) * (lowLevel > lowEnv ? att : rel);
            breathSlow += (lowEnv - breathSlow) * slow;
            const float voiced = juce::jlimit(0.0f, 1.0f, (lowEnv / std::max(1e-6f, breathEnv) - 0.35f) / 0.35f);
            const float soft = juce::jlimit(0.0f, 1.0f, 1.0f - lowEnv / std::max(1e-5f, breathSlow));
            const float add = nz * lowEnv * voiced * breath * 0.22f * (1.0f + response * 0.6f * soft);
            for (int c = 0; c < ch; ++c) buffer.addSample(c, i, add);
        }
    }

    // ---- tone ----
    // smart tone: a deeper voice gets the chest it would really have, a lighter one some air,
    // and moving the formants down is balanced so the voice does not turn dull
    float bodyAuto = 0, airAuto = shape.air;
    if (param(ids::autoTone) > 0.5f) {
        bodyAuto = juce::jlimit(-3.0f, 4.0f, -0.25f * p);
        airAuto += juce::jlimit(-3.0f, 2.5f, 0.05f * p - 0.3f * f);
    }
    updateFilters(bodyAuto, airAuto);
    if (lastNasal > 0.001f) {
        nasalPeak.process(juce::dsp::ProcessContextReplacing<float>(wet));
        nasalLow.process(juce::dsp::ProcessContextReplacing<float>(wet));
    }
    if (std::abs(lastBody) > 0.05f) bodyEq.process(juce::dsp::ProcessContextReplacing<float>(wet));
    if (std::abs(lastPresence) > 0.05f) presenceEq.process(juce::dsp::ProcessContextReplacing<float>(wet));
    if (std::abs(lastAir) > 0.05f) airEq.process(juce::dsp::ProcessContextReplacing<float>(wet));

    // ---- keep level: the finished voice follows the original's loudness (slow, about 300 ms) ----
    if (param(ids::loudness) > 0.5f) {
        const float k = 1.0f / (0.3f * (float) sr);
        for (int i = 0; i < n; ++i) {
            float d = 0, w = 0;
            for (int c = 0; c < ch; ++c) {
                d += dry.getSample(c, i) * dry.getSample(c, i);
                w += buffer.getSample(c, i) * buffer.getSample(c, i);
            }
            levelDry += (d - levelDry) * k;
            levelWet += (w - levelWet) * k;
            // only while both are really sounding: a silent wet path (an engine starting up) must not pump the gain
            const bool both = levelDry > 1e-7f && levelWet > levelDry * 0.05f;
            const float target = both ? juce::jlimit(0.25f, 2.0f, std::sqrt(levelDry / levelWet)) : levelGain;
            levelGain += (target - levelGain) * k * 0.5f;
            for (int c = 0; c < ch; ++c) buffer.setSample(c, i, buffer.getSample(c, i) * levelGain);
        }
    }

    // ---- mix and output ----
    mixSm.setTargetValue(param(ids::mix));
    gainSm.setTargetValue(juce::Decibels::decibelsToGain(param(ids::output)));
    // A safety limiter at -0.3 dBFS closes the chain: it only acts on overs (a growl, a boosted
    // chest), catching them at once and letting go over 80 ms, linked across channels.
    const float ceiling = 0.966f, release = 1.0f - std::exp(-1.0f / (0.08f * (float) sr));
    float outPeak = 0;
    for (int i = 0; i < n; ++i) {
        const float mx = mixSm.getNextValue(), g = gainSm.getNextValue();
        float peak = 0;
        for (int c = 0; c < ch; ++c) {
            float v = (buffer.getSample(c, i) * mx + dry.getSample(c, i) * (1.0f - mx)) * g;
            if (!std::isfinite(v)) v = 0.0f; // never pass a NaN or infinity to the host
            buffer.setSample(c, i, v);
            peak = std::max(peak, std::abs(v));
        }
        const float need = peak * limitGain > ceiling ? ceiling / peak : 1.0f;
        limitGain = need < limitGain ? need : limitGain + (1.0f - limitGain) * release;
        for (int c = 0; c < ch; ++c) buffer.setSample(c, i, buffer.getSample(c, i) * limitGain);
        outPeak = std::max(outPeak, peak * limitGain);
    }
    outputLevel.store(outPeak);
}

void AlptugDUBProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void AlptugDUBProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* AlptugDUBProcessor::createEditor() { return new AlptugDUBEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AlptugDUBProcessor(); }
