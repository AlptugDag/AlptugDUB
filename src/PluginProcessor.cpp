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
} // namespace

const std::vector<AlptugDUBProcessor::Character>& AlptugDUBProcessor::characters() {
    // targetF0: the character's speaking pitch. formant: its vocal tract, in semitones above an adult man's.
    static const std::vector<Character> list = {
        //  id            Türkçe          English         F0    pitch tract  range growl breath age  nasal body  air
        {"natural",     "Doğal",        "Natural",       0,    0,    0,    1.00, 0.00, 0.00, 0.00, 0.00, 0.0,  0.0},
        {"bigman",      "İri adam",     "Big man",       88,  -4,  -2.5,  0.85, 0.08, 0.00, 0.00, 0.00, 3.0, -1.0},
        {"giant",       "Dev",          "Giant",         70,  -8,  -5.0,  0.70, 0.20, 0.05, 0.00, 0.00, 4.0, -2.0},
        {"monster",     "Canavar",      "Monster",       62, -10,  -4.0,  0.55, 0.75, 0.15, 0.15, 0.00, 4.0, -1.0},
        {"man",         "Erkek",        "Man",          115,  -7,   0.0,  0.95, 0.00, 0.00, 0.00, 0.00, 1.5,  0.0},
        {"woman",       "Kadın",        "Woman",        205,   7,   2.2,  1.10, 0.00, 0.08, 0.00, 0.00, 0.0,  1.0},
        {"girl",        "Küçük kız",    "Little girl",  300,   9,   4.5,  1.30, 0.00, 0.12, 0.00, 0.05,-2.0,  2.0},
        {"boy",         "Küçük oğlan",  "Little boy",   265,   7,   4.0,  1.25, 0.00, 0.08, 0.00, 0.00,-1.5,  1.5},
        {"oldman",      "Yaşlı adam",   "Old man",      125,  -1,   0.0,  0.90, 0.05, 0.30, 0.55, 0.10, 0.0, -1.0},
        {"oldwoman",    "Yaşlı kadın",  "Old woman",    185,   4,   2.0,  1.00, 0.00, 0.30, 0.55, 0.15, 0.0, -1.0},
        {"witch",       "Cadı",         "Witch",        235,   5,   2.6,  1.45, 0.12, 0.35, 0.45, 0.60,-2.0,  1.5},
        {"dwarf",       "Cüce",         "Dwarf",        120,  -2,   2.5,  0.90, 0.15, 0.00, 0.10, 0.20, 1.0,  0.0},
        {"goblin",      "Goblin",       "Goblin",       270,   6,   3.5,  1.35, 0.35, 0.10, 0.10, 0.55,-2.0,  1.0},
    };
    return list;
}

juce::AudioProcessorValueTreeState::ParameterLayout AlptugDUBProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto st = [](float v, int) { return String(v, 2) + " st"; };
    auto pct = [](float v, int) { return String(juce::roundToInt(v * 100)) + "%"; };
    auto db = [](float v, int) { return String(v, 1) + " dB"; };
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::pitch, 1}, "Pitch", NormalisableRange<float>(-12, 12, 0.01f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(st)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::formant, 1}, "Formant", NormalisableRange<float>(-8, 8, 0.01f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(st)));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::follow, 1}, "Formant follows pitch", true));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::range, 1}, "Intonation", NormalisableRange<float>(0.3f, 2.0f, 0.01f), 1.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::growl, 1}, "Growl", NormalisableRange<float>(0, 1, 0.01f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::breath, 1}, "Breath", NormalisableRange<float>(0, 1, 0.01f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::age, 1}, "Age", NormalisableRange<float>(0, 1, 0.01f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::nasal, 1}, "Nasal", NormalisableRange<float>(0, 1, 0.01f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::body, 1}, "Body", NormalisableRange<float>(-6, 6, 0.1f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::air, 1}, "Air", NormalisableRange<float>(-6, 6, 0.1f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::mix, 1}, "Mix", NormalisableRange<float>(0, 1, 0.01f), 1.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::output, 1}, "Output", NormalisableRange<float>(-12, 12, 0.1f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ids::engine, 1}, "Engine", StringArray{"Natural voice", "Wide"}, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::loudness, 1}, "Keep loudness", true));
    StringArray names;
    for (auto& c : characters()) names.add(c.nameEn);
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ids::character, 1}, "Character", names, 0));
    return l;
}

AlptugDUBProcessor::AlptugDUBProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "AlptugDUB", layout()) {}

bool AlptugDUBProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void AlptugDUBProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sr = sampleRate;
    const int ch = getTotalNumOutputChannels();
    psola.prepare(sampleRate, ch);
    spectral.prepare(sampleRate, ch, samplesPerBlock);
    tracker.prepare(sampleRate);
    profile.prepare(sampleRate);
    hop = std::max(1, (int) std::lround(sampleRate * 0.005));
    latency = std::max(psola.latencySamples(), spectral.latencySamples());
    setLatencySamples(latency);

    juce::dsp::ProcessSpec spec{sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) ch};
    for (auto* d : {&dryDelay, &padPsola, &padWide}) {
        d->prepare(spec);
        d->reset();
    }
    dryDelay.setDelay((float) latency);
    padPsola.setDelay((float) (latency - psola.latencySamples()));
    padWide.setDelay((float) (latency - spectral.latencySamples()));
    dry.setSize(ch, samplesPerBlock);
    breathNoise.setSize(1, samplesPerBlock);
    for (auto* f : {&bodyEq, &airEq, &nasalPeak, &nasalLow}) {
        f->prepare(spec);
        f->reset();
    }
    noiseHp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 1600.0, 0.7);
    noiseLp.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 7500.0, 0.7);
    noiseHp.reset();
    noiseLp.reset();
    lastBody = lastAir = lastNasal = 1000;
    for (auto* s : {&pitchSm, &formantSm, &mixSm, &gainSm}) s->reset(sampleRate, 0.05);
    pitchSm.setCurrentAndTargetValue(state.getRawParameterValue(ids::pitch)->load());
    formantSm.setCurrentAndTargetValue(state.getRawParameterValue(ids::formant)->load());
    mixSm.setCurrentAndTargetValue(state.getRawParameterValue(ids::mix)->load());
    gainSm.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(state.getRawParameterValue(ids::output)->load()));
}

void AlptugDUBProcessor::updateFilters() {
    const float body = state.getRawParameterValue(ids::body)->load();
    const float air = state.getRawParameterValue(ids::air)->load();
    const float nasal = state.getRawParameterValue(ids::nasal)->load();
    if (body != lastBody) {
        *bodyEq.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, 170.0, 0.7, juce::Decibels::decibelsToGain(body));
        lastBody = body;
    }
    if (air != lastAir) {
        *airEq.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, 9000.0, 0.7, juce::Decibels::decibelsToGain(air));
        lastAir = air;
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
        pitch = juce::jlimit(-12.0f, 12.0f, 12.0f * std::log2(c.targetF0 / s.medianF0));
        formant = c.formant - speakerTract(s.medianF0);
    }
    if (index == 0) pitch = formant = 0;
    auto set = [&](const char* id, float v) {
        if (auto* p = state.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    set(ids::pitch, pitch);
    set(ids::formant, juce::jlimit(-8.0f, 8.0f, formant));
    set(ids::follow, 0.0f);
    set(ids::range, c.range);
    set(ids::growl, c.growl);
    set(ids::breath, c.breath);
    set(ids::age, c.age);
    set(ids::nasal, c.nasal);
    set(ids::body, c.body);
    set(ids::air, c.air);
    set(ids::character, (float) index);
}

void AlptugDUBProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), ch = buffer.getNumChannels();
    if (n == 0) return;
    if (dry.getNumSamples() < n) {
        dry.setSize(ch, n, false, false, true);
        breathNoise.setSize(1, n, false, false, true);
    }

    // ---- learn the voice (input side) ----
    if (resetProfile.exchange(false)) profile.reset();
    double inSum = 0;
    bool voicedNow = liveF0.load() > 0;
    for (int i = 0; i < n; ++i) {
        float m = 0;
        for (int c = 0; c < ch; ++c) m += buffer.getSample(c, i);
        m /= (float) ch;
        inSum += m * m;
        tracker.push(m);
        breathNoise.setSample(0, i, m);
        if (++hopCount >= hop) {
            hopCount = 0;
            const auto e = tracker.estimate();
            profile.addEstimate(e, hop);
            voicedNow = e.voiced() && e.rms > 0.003f;
            liveF0.store(voicedNow ? (float) (sr / e.period) : 0.0f);
        }
    }
    profile.addAudio(breathNoise.getReadPointer(0), n, voicedNow);
    {
        const juce::SpinLock::ScopedTryLockType lock(profileLock);
        if (lock.isLocked()) summaryCopy = profile.summary();
    }
    inputLevel.store((float) std::sqrt(inSum / n));

    // ---- dry copy, aligned with the processed signal ----
    for (int c = 0; c < ch; ++c) dry.copyFrom(c, 0, buffer, c, 0, n);
    {
        juce::dsp::AudioBlock<float> b(dry.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        dryDelay.process(juce::dsp::ProcessContextReplacing<float>(b));
    }

    // ---- the voice engine ----
    pitchSm.setTargetValue(state.getRawParameterValue(ids::pitch)->load());
    formantSm.setTargetValue(state.getRawParameterValue(ids::formant)->load());
    pitchSm.skip(n);
    formantSm.skip(n);
    const float p = pitchSm.getCurrentValue();
    float f = formantSm.getCurrentValue();
    if (state.getRawParameterValue(ids::follow)->load() > 0.5f) f += 0.25f * p;
    const float median = summaryCopy.medianF0;
    const bool wideEngine = state.getRawParameterValue(ids::engine)->load() > 0.5f;
    juce::dsp::AudioBlock<float> wet(buffer);
    if (!wideEngine) {
        psola.setRatios(semis(p), semis(f));
        psola.setCharacter(state.getRawParameterValue(ids::range)->load(), state.getRawParameterValue(ids::growl)->load(),
                           state.getRawParameterValue(ids::age)->load(), median);
        psola.setLoudnessMatch(state.getRawParameterValue(ids::loudness)->load() > 0.5f);
        psola.process(buffer.getArrayOfWritePointers(), n);
        padPsola.process(juce::dsp::ProcessContextReplacing<float>(wet));
    } else {
        spectral.set(p, f, median);
        spectral.process(buffer.getArrayOfWritePointers(), n);
        padWide.process(juce::dsp::ProcessContextReplacing<float>(wet));
    }

    // ---- breath: aspiration noise riding on the voice's own envelope ----
    const float breath = state.getRawParameterValue(ids::breath)->load();
    if (breath > 0.001f) {
        auto* nz = breathNoise.getWritePointer(0);
        for (int i = 0; i < n; ++i) nz[i] = noiseLp.processSample(noiseHp.processSample(rng.nextFloat() * 2.0f - 1.0f));
        const float att = 1.0f - std::exp(-1.0f / (0.003f * (float) sr)), rel = 1.0f - std::exp(-1.0f / (0.060f * (float) sr));
        for (int i = 0; i < n; ++i) {
            float level = 0;
            for (int c = 0; c < ch; ++c) level = std::max(level, std::abs(buffer.getSample(c, i)));
            breathEnv += (level - breathEnv) * (level > breathEnv ? att : rel);
            const float add = nz[i] * breathEnv * breath * 0.9f;
            for (int c = 0; c < ch; ++c) buffer.addSample(c, i, add);
        }
    }

    // ---- tone ----
    updateFilters();
    if (lastNasal > 0.001f) {
        nasalPeak.process(juce::dsp::ProcessContextReplacing<float>(wet));
        nasalLow.process(juce::dsp::ProcessContextReplacing<float>(wet));
    }
    if (std::abs(lastBody) > 0.05f) bodyEq.process(juce::dsp::ProcessContextReplacing<float>(wet));
    if (std::abs(lastAir) > 0.05f) airEq.process(juce::dsp::ProcessContextReplacing<float>(wet));

    // ---- mix and output ----
    mixSm.setTargetValue(state.getRawParameterValue(ids::mix)->load());
    gainSm.setTargetValue(juce::Decibels::decibelsToGain(state.getRawParameterValue(ids::output)->load()));
    double outSum = 0;
    for (int i = 0; i < n; ++i) {
        const float mx = mixSm.getNextValue(), g = gainSm.getNextValue();
        for (int c = 0; c < ch; ++c) {
            const float v = (buffer.getSample(c, i) * mx + dry.getSample(c, i) * (1.0f - mx)) * g;
            buffer.setSample(c, i, v);
            if (c == 0) outSum += v * v;
        }
    }
    outputLevel.store((float) std::sqrt(outSum / n));
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
