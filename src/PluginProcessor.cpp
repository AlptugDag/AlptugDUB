// AlptugDUB by Alptuğ Dağ — v3.0 studio audio processor.
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
/** The speaker's vocal tract, as semitones above an adult man's, smoothly estimated from F0. */
float speakerTract(float medianF0) {
    if (medianF0 <= 125.0f) return 0.0f;
    return juce::jlimit(0.0f, 4.0f, 2.2f * std::log2(medianF0 / 125.0f) / std::log2(225.0f / 125.0f));
}

/** Perceptually anchored character pitch & formant target for any speaker (prevents over-shifting). */
std::pair<float, float> anchoredCharacterShift(const AlptugDUBProcessor::Character& c, int index, const adub::VoiceSummary& s,
                                               float spkF0) {
    float pitch = c.pitch;
    float formant = c.formant;
    if (index == 0 || c.targetF0 <= 0.0f || spkF0 <= 40.0f) return {pitch, formant};

    const bool maleSpeaker = s.type != adub::VoiceType::Unknown ? (s.type <= adub::VoiceType::Tenor) : (spkF0 < 165.0f);
    float effTarget = c.targetF0;
    if (maleSpeaker && c.pitch > 3.0f && c.targetF0 < 255.0f) effTarget = 270.0f;

    const float adaptivePitch = 12.0f * std::log2(effTarget / spkF0);
    if (!maleSpeaker && c.pitch > 3.0f && c.targetF0 < 235.0f) {
        pitch = juce::jlimit(-2.0f, 2.0f, 0.85f * adaptivePitch + 0.15f * c.pitch);
    } else if (c.pitch < 0.0f) {
        // Asymmetric cross-gender range lets a higher voice reach natural male pitch cleanly without crashing below 64 Hz
        const float minP = std::max(c.pitch - 7.5f, 12.0f * std::log2(std::max(66.0f, c.targetF0 * 0.92f) / spkF0));
        const float maxP = c.pitch + 3.0f;
        pitch = juce::jlimit(std::min(minP, maxP), maxP, 0.72f * adaptivePitch + 0.28f * c.pitch);
    } else {
        const float minP = (maleSpeaker && c.pitch > 3.0f) ? std::max(4.0f, c.pitch - 2.0f) : c.pitch - 3.0f;
        const float maxP = c.pitch + 3.5f;
        pitch = juce::jlimit(minP, maxP, 0.65f * adaptivePitch + 0.35f * c.pitch);
    }

    const float spkTract = s.ready() ? s.tractSemitones() : speakerTract(spkF0);
    formant = juce::jlimit(c.formant - 2.2f, c.formant + 2.0f, c.formant - spkTract);
    return {pitch, formant};
}

/**
 * Age, -50 (younger) .. +50 (older): a younger voice sits a little higher with a shorter vocal tract
 * and more air; an older one sounds a little lower and darker.
 */
struct AgeShape {
    float pitch = 0, formant = 0, air = 0;
};
AgeShape ageShape(float age) {
    const float a = juce::jlimit(-1.0f, 1.0f, age / 50.0f);
    AgeShape s;
    if (a < 0) {
        s.pitch = -1.0f * a;
        s.formant = -1.2f * a;
        s.air = -1.5f * a;
    } else {
        s.formant = -0.3f * a;
        s.air = -0.5f * a;
    }
    return s;
}
} // namespace

const std::vector<AlptugDUBProcessor::Character>& AlptugDUBProcessor::characters() {
    // targetF0: the character's speaking pitch. formant: its vocal tract, in semitones above an adult man's.
    static const std::vector<Character> list = {
        //  id          Türkçe          English        F0    pitch   tract    age  nasal  body   air    regen  glottal  growl  breath
        {"natural",   "Doğal",        "Natural",       0,    0.0f,  0.0f,    0, 0.00f,  0.0f,  0.0f, 0.00f,  0.00f, 0.00f, 0.00f},
        {"bigman",    "İri adam",     "Big man",      96,   -4.5f, -2.8f,    3, 0.00f,  2.0f,  0.5f, 0.92f, -0.55f, 0.18f, 0.05f},
        {"giant",     "Dev",          "Giant",        76,   -7.5f, -4.5f,    6, 0.00f,  3.0f,  2.5f, 0.96f, -0.80f, 0.38f, 0.08f},
        {"monster",   "Canavar",      "Monster",      66,   -9.5f, -4.2f,    9, 0.00f,  3.0f,  3.0f, 1.00f, -0.90f, 0.68f, 0.16f},
        {"man",       "Erkek",        "Man",         115,   -3.4f, -0.8f,    0, 0.00f,  1.0f,  0.5f, 0.90f, -0.30f, 0.06f, 0.04f},
        {"woman",     "Kadın",        "Woman",       215,    5.6f,  2.6f,    0, 0.00f,  0.0f,  0.5f, 0.95f,  0.55f, 0.00f, 0.14f},
        {"girl",      "Küçük kız",    "Little girl", 295,    8.0f,  4.6f,  -40, 0.03f, -2.0f,  0.5f, 0.96f,  0.75f, 0.00f, 0.18f},
        {"boy",       "Küçük oğlan",  "Little boy",  255,    6.5f,  3.8f,  -35, 0.00f, -1.5f,  0.5f, 0.94f,  0.45f, 0.00f, 0.12f},
        {"oldman",    "Yaşlı adam",   "Old man",     124,   -2.2f, -0.6f,   18, 0.05f,  0.0f, -1.0f, 0.92f, -0.25f, 0.28f, 0.18f},
        {"oldwoman",  "Yaşlı kadın",  "Old woman",   195,    3.8f,  1.8f,   18, 0.07f,  0.0f, -1.0f, 0.94f,  0.30f, 0.18f, 0.22f},
        {"witch",     "Cadı",         "Witch",       235,    5.0f,  2.4f,   15, 0.30f, -2.0f,  0.5f, 0.96f, -0.45f, 0.42f, 0.20f},
        {"dwarf",     "Cüce",         "Dwarf",       120,   -2.4f,  2.2f,    3, 0.10f,  1.0f,  0.0f, 0.92f, -0.65f, 0.30f, 0.06f},
        {"goblin",    "Goblin",       "Goblin",      265,    6.0f,  3.4f,    3, 0.28f, -2.0f,  0.0f, 0.96f, -0.50f, 0.48f, 0.14f},
    };
    return list;
}

juce::AudioProcessorValueTreeState::ParameterLayout AlptugDUBProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto st = [](float v, int) { return (v > 0.004f ? "+" : "") + String(v, 1) + " st"; };
    auto pct = [](float v, int) { return String(roundToInt(v * 100)) + "%"; };
    auto signedPct = [](float v, int) {
        const int p = roundToInt(v * 100);
        return (p > 0 ? "+" : "") + String(p) + "%";
    };
    auto db = [](float v, int) { return (v > 0.04f ? "+" : "") + String(v, 1) + " dB"; };
    auto dbRaw = [](float v, int) { return String(v, 0) + " dB"; };
    auto hz = [](float v, int) { return String(roundToInt(v)) + " Hz"; };
    auto ms = [](float v, int) { return String(roundToInt(v)) + " ms"; };
    auto years = [](float v, int) { return v > 0.5f ? "+" + String(roundToInt(v)) : String(roundToInt(v)); };
    auto addFloat = [&](const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String(float, int)> text) {
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction(text)));
    };
    addFloat(ids::pitch, "Pitch", {-18, 18, 0.01f}, 0.0f, st);
    addFloat(ids::formant, "Formant", {-8, 8, 0.01f}, 0.0f, st);
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::follow, 1}, "Formant follows pitch", false));
    addFloat(ids::age, "Age", {-50, 50, 1}, 0.0f, years);
    addFloat(ids::nasal, "Nasal", {0, 1, 0.01f}, 0.0f, pct);
    addFloat(ids::presence, "Presence", {-6, 6, 0.1f}, 0.0f, db);
    addFloat(ids::body, "Body", {-6, 6, 0.1f}, 0.0f, db);
    addFloat(ids::air, "Air", {-6, 6, 0.1f}, 0.0f, db);
    addFloat(ids::regen, "Regen", {0, 1, 0.01f}, 1.0f, pct);
    addFloat(ids::glottal, "Glottal", {-1, 1, 0.01f}, 0.0f, signedPct);
    addFloat(ids::growl, "Growl", {0, 1, 0.01f}, 0.0f, pct);
    addFloat(ids::breath, "Breath", {0, 1, 0.01f}, 0.10f, pct);
    addFloat(ids::autotune, "Auto-Tune", {0, 1, 0.01f}, 0.0f, pct);
    addFloat(ids::mix, "Mix", {0, 1, 0.01f}, 1.0f, pct);
    addFloat(ids::output, "Output", {-12, 12, 0.1f}, 0.0f, db);
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::loudness, 1}, "Keep loudness", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::ai, 1}, "AI analysis", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoMatch, 1}, "Auto match character", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoTone, 1}, "Smart tone", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoTract, 1}, "Natural tract motion", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoResponse, 1}, "Dynamic response", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::autoSteady, 1}, "Steady pitch", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::consonantGuard, 1}, "Consonant guard", true));

    // v3.0 studio pre-conditioning & Auto-Tune parameters
    addFloat(ids::hpfHz, "Input HPF", {40.0f, 160.0f, 1.0f}, 75.0f, hz);
    addFloat(ids::gateDb, "Noise Gate", {-72.0f, -24.0f, 0.5f}, -52.0f, dbRaw);
    addFloat(ids::deEsser, "De-Esser", {0.0f, 1.0f, 0.01f}, 0.35f, pct);
    addFloat(ids::retuneSpeedMs, "Retune Speed", {5.0f, 120.0f, 1.0f}, 22.0f, ms);
    addFloat(ids::vibratoPreserve, "Vibrato Preserve", {0.0f, 1.0f, 0.01f}, 0.75f, pct);
    l.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ids::scaleMode, 1}, "Scale Mode", StringArray{"Auto", "Major", "Minor", "Chromatic", "Hijaz", "Pentatonic"}, 0));
    l.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ids::manualKey, 1}, "Manual Key", StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));

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
    state.addParameterListener(ids::pitch, this);
    state.addParameterListener(ids::formant, this);
    state.addParameterListener(ids::follow, this);
    state.addParameterListener(ids::autoMatch, this);
    startTimerHz(15);
}

AlptugDUBProcessor::~AlptugDUBProcessor() {
    stopTimer();
    state.removeParameterListener(ids::pitch, this);
    state.removeParameterListener(ids::formant, this);
    state.removeParameterListener(ids::follow, this);
    state.removeParameterListener(ids::autoMatch, this);
}

bool AlptugDUBProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void AlptugDUBProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sr = sampleRate;
    const int ch = std::max(1, getTotalNumOutputChannels());
    // Input HPF is applied once upstream in processBlock, so disable duplicate internal HPF inside yin
    yin.prepare(sampleRate, 0.0);
    neural.prepare(sampleRate);
    autoTune.prepare(sampleRate);
    regenerator.prepare(sampleRate, ch, samplesPerBlock);
    profile.prepare(sampleRate);
    softGate.prepare(sampleRate);
    loudnessMatcher.prepare(sampleRate);
    limiter.prepare(sampleRate, ch, -0.3f, 3.0f);

    hop = std::max(1, (int) std::lround(sampleRate * 0.005));
    regenLatency = regenerator.latencySamples();
    latency = regenLatency + limiter.latencySamples();
    setLatencySamples(latency);

    juce::dsp::ProcessSpec spec{sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) ch};
    dryDelay.prepare(spec);
    dry.setSize(ch, samplesPerBlock);
    rawDry.setSize(ch, samplesPerBlock);
    mono.setSize(1, samplesPerBlock);
    for (auto* f : {&inputHpf, &bodyEq, &airEq, &presenceEq, &nasalPeak, &nasalLow}) {
        f->prepare(spec);
        f->reset();
    }
    lastHpfHz = -1.0f;
    lastBody = lastAir = lastNasal = lastPresence = 1000.0f;
    for (auto* s : {&pitchSm, &formantSm, &regenSm, &glottalSm, &growlSm, &breathSm, &autotuneSm, &mixSm, &gainSm})
        s->reset(sampleRate, 0.05);
    reset();
}

void AlptugDUBProcessor::reset() {
    if (mono.getNumSamples() == 0) return; // not prepared yet
    regenerator.reset();
    autoTune.reset();
    yin.reset();
    neural.reset();
    softGate.reset();
    loudnessMatcher.reset();
    limiter.reset();
    inputHpf.reset();
    hopCount = 0;
    // dryDelay aligns dry with wet at the mix node BEFORE the shared 3 ms lookahead limiter
    dryDelay.setDelay((float) regenLatency);
    dryDelay.reset();
    liveLimiterGrDb.store(0.0f);
    melodyFast = melodySlow = fastF0 = neuralF0 = neuralConf = fastConf = 0;
    effortFast = 0;
    effortSlow = 1e-4f;
    lastPresenceDyn = 0;
    fastLogF0 = 0;
    voicedHopCount = 0;
    activeAutoPitch = param(ids::pitch);
    activeAutoFormant = param(ids::formant);
    lastLinkedPitch = activeAutoPitch;
    lastAutoSetPitch = activeAutoPitch;
    lastAutoSetFormant = activeAutoFormant;
    hasPendingAuto.store(false);
    for (auto& w : waveHistory) w.store(0.0f);
    waveWritePos.store(0);
    pitchSm.setCurrentAndTargetValue(param(ids::pitch));
    formantSm.setCurrentAndTargetValue(param(ids::formant));
    regenSm.setCurrentAndTargetValue(param(ids::regen));
    glottalSm.setCurrentAndTargetValue(param(ids::glottal));
    growlSm.setCurrentAndTargetValue(param(ids::growl));
    breathSm.setCurrentAndTargetValue(param(ids::breath));
    autotuneSm.setCurrentAndTargetValue(param(ids::autotune));
    mixSm.setCurrentAndTargetValue(param(ids::mix));
    gainSm.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(param(ids::output)));
}

void AlptugDUBProcessor::updateFilters(float bodyExtra, float airExtra, float presenceExtra) {
    const float hpf = juce::jlimit(40.0f, 160.0f, param(ids::hpfHz));
    const float body = juce::jlimit(-9.0f, 9.0f, param(ids::body) + bodyExtra);
    const float air = juce::jlimit(-9.0f, 9.0f, param(ids::air) + airExtra);
    const float nasal = param(ids::nasal);
    const float presence = juce::jlimit(-9.0f, 9.0f, param(ids::presence) + presenceExtra);
    if (std::abs(hpf - lastHpfHz) > 0.25f) {
        *inputHpf.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, (double) hpf, 0.70710678);
        lastHpfHz = hpf;
    }
    if (std::abs(body - lastBody) > 0.01f) {
        *bodyEq.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, 170.0, 0.7, juce::Decibels::decibelsToGain(body));
        lastBody = body;
    }
    if (std::abs(air - lastAir) > 0.01f) {
        *airEq.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, std::min(9000.0, sr * 0.4), 0.7, juce::Decibels::decibelsToGain(air));
        lastAir = air;
    }
    if (std::abs(presence - lastPresence) > 0.05f) {
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

void AlptugDUBProcessor::parameterChanged(const juce::String& id, float newValue) {
    if (id == ids::autoMatch) {
        if (newValue > 0.5f) userTookOver.store(false);
        return;
    }
    if (id == ids::follow) {
        if (newValue > 0.5f && std::abs(param(ids::formant)) < 0.05f && std::abs(param(ids::pitch)) > 0.1f)
            lastLinkedPitch = 0.0f;
        else
            lastLinkedPitch = param(ids::pitch);
        return;
    }
    if (suppressLink.load(std::memory_order_relaxed)) return;
    if (id == ids::pitch && std::abs(newValue - lastAutoSetPitch) > 0.15f) userTookOver.store(true);
    if (id == ids::formant && std::abs(newValue - lastAutoSetFormant) > 0.15f) userTookOver.store(true);
}

void AlptugDUBProcessor::applyCharacter(int index) {
    const auto& list = characters();
    if (index < 0 || index >= (int) list.size()) return;
    const Character& c = list[(size_t) index];
    const auto s = voiceSummary();
    auto [pitch, formant] = anchoredCharacterShift(c, index, s, s.ready() ? s.medianF0 : 0.0f);
    // age moves pitch and tract by itself; take that out so the character lands where it should
    const auto shape = ageShape(c.age);
    pitch -= shape.pitch;
    formant -= shape.formant;
    if (index == 0) pitch = formant = 0;

    const float clampedP = juce::jlimit(-18.0f, 18.0f, pitch);
    const float clampedF = juce::jlimit(-8.0f, 8.0f, formant);

    suppressLink.store(true);
    auto set = [&](const char* id, float v) {
        if (auto* p = state.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    userTookOver.store(false);
    hasPendingAuto.store(false);
    lastAutoSetPitch = clampedP;
    lastAutoSetFormant = clampedF;
    lastLinkedPitch = clampedP;
    set(ids::pitch, clampedP);
    set(ids::formant, clampedF);
    set(ids::age, c.age);
    set(ids::nasal, c.nasal);
    set(ids::body, c.body);
    set(ids::air, c.air);
    set(ids::regen, c.regen);
    set(ids::glottal, c.glottal);
    set(ids::growl, c.growl);
    set(ids::breath, c.breath);
    set(ids::character, (float) index);
    suppressLink.store(false);
}

void AlptugDUBProcessor::timerCallback() {
    // Only sync Auto-Match parameters back onto APVTS when an editor UI is open (keeps headless auval strict)
    if (getActiveEditor() == nullptr) return;
    if (!hasPendingAuto.exchange(false)) return;
    if (param(ids::autoMatch) < 0.5f || userTookOver.load()) return;
    const int index = juce::roundToInt(param(ids::character));
    if (index <= 0 || index >= (int) characters().size()) return;

    const float np = pendingAutoPitch.load();
    const float nf = pendingAutoFormant.load();
    if (std::abs(np - param(ids::pitch)) > 0.15f || std::abs(nf - param(ids::formant)) > 0.15f) {
        suppressLink.store(true);
        lastAutoSetPitch = np;
        lastAutoSetFormant = nf;
        lastLinkedPitch = np;
        if (auto* pp = state.getParameter(ids::pitch)) pp->setValueNotifyingHost(pp->convertTo0to1(np));
        if (auto* fp = state.getParameter(ids::formant)) fp->setValueNotifyingHost(fp->convertTo0to1(nf));
        suppressLink.store(false);
    }
}

void AlptugDUBProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), ch = buffer.getNumChannels();
    if (n == 0 || ch == 0) return;
    if (dry.getNumSamples() < n || dry.getNumChannels() < ch) {
        dry.setSize(ch, n, false, false, true); // only if the host breaks its promised block size
        rawDry.setSize(ch, n, false, false, true);
        mono.setSize(1, n, false, false, true);
    }

    // ---- Stage 0: Input HPF + Soft Noise Gate pre-conditioning (strips <40..160 Hz rumble before YIN/AI) ----
    const float hpf = juce::jlimit(40.0f, 160.0f, param(ids::hpfHz));
    if (std::abs(hpf - lastHpfHz) > 0.25f) {
        *inputHpf.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, (double) hpf, 0.70710678);
        lastHpfHz = hpf;
    }

    float inPeak = 0.0f;
    for (int c = 0; c < ch; ++c) {
        rawDry.copyFrom(c, 0, buffer, c, 0, n);
        dry.copyFrom(c, 0, buffer, c, 0, n);
    }
    {
        juce::dsp::AudioBlock<float> dryBlk(dry.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        dryDelay.process(juce::dsp::ProcessContextReplacing<float>(dryBlk));

        juce::dsp::AudioBlock<float> condBlk(rawDry.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        inputHpf.process(juce::dsp::ProcessContextReplacing<float>(condBlk));
    }

    const float gateDb = param(ids::gateDb);
    auto* m = mono.getWritePointer(0);
    for (int i = 0; i < n; ++i) {
        float sRaw = 0.0f, sCond = 0.0f;
        for (int c = 0; c < ch; ++c) {
            sRaw += buffer.getSample(c, i);
            sCond += rawDry.getSample(c, i);
        }
        sRaw /= (float) ch;
        sCond /= (float) ch;
        inPeak = std::max(inPeak, std::abs(sRaw));
        const float gGate = softGate.step(std::abs(sCond), gateDb);
        for (int c = 0; c < ch; ++c) rawDry.setSample(c, i, rawDry.getSample(c, i) * gGate);
        m[i] = sCond * gGate;
    }
    inputLevel.store(inPeak);

    // ---- listen: neural pitch model (AI) or conditioned YIN tracker, for the voice profile ----
    if (resetProfile.exchange(false)) {
        profile.reset();
        regenerator.resetVoiceProfile();
        autoTune.resetProfile();
        fastLogF0 = 0.0f;
        voicedHopCount = 0;
        melodyFast = melodySlow = 0.0f;
        userTookOver.store(false);
        hasPendingAuto.store(false);
    }
    const bool useNeural = param(ids::ai) > 0.5f;
    bool voicedNow = false;
    for (int i = 0; i < n; ++i) {
        const float s = m[i];
        yin.push(s);
        if (++hopCount >= hop) {
            hopCount = 0;
            const auto e = yin.estimate();
            regenerator.pushYinEstimate(e);
            if (e.voiced()) profile.addBreath(e.aperiodicity);
            voicedNow = e.voiced() && e.rms > 0.003f;
            fastF0 = voicedNow ? (float) (sr / e.period) : 0.0f;
            fastConf = e.confidence > 0.0f ? e.confidence : (e.voiced() ? juce::jlimit(0.0f, 1.0f, 1.0f - e.aperiodicity / 0.45f) : 0.0f);
            if (fastF0 > 45.0f && fastF0 < 850.0f) {
                const float lf = std::log(fastF0);
                if (melodyFast <= 0 || melodySlow <= 0) melodyFast = melodySlow = lf;
                melodyFast += (lf - melodyFast) * 0.14f;
                melodySlow += (lf - melodySlow) * 0.008f;
                ++voicedHopCount;
                if (voicedHopCount == 1 || fastLogF0 <= 0.0f) fastLogF0 = lf;
                else {
                    const float alpha = voicedHopCount <= 16 ? 0.25f : 0.015f;
                    fastLogF0 += (lf - fastLogF0) * alpha;
                }
            }
            if (!useNeural) {
                profile.addEstimate(e, hop);
                liveF0.store(fastF0);
            }
        }
        if (useNeural && neural.push(s)) {
            const auto& e = neural.latest();
            regenerator.pushNeuralEstimate(e);
            profile.addEstimate(e, neural.spacing());
            neuralF0 = e.voiced() ? (float) (sr / e.period) : 0.0f;
            neuralConf = e.confidence;
            liveF0.store(neuralF0 > 0.0f ? neuralF0 : fastF0);
            liveConfidence.store(neuralConf);
        }
    }
    const float blockSec = (float) n / (float) sr;
    {
        float e2 = 0;
        for (int i = 0; i < n; ++i) e2 += m[i] * m[i];
        const float rmsNow = std::sqrt(e2 / (float) n);
        effortFast += (rmsNow - effortFast) * (1.0f - std::exp(-blockSec / 0.06f));
        if (fastF0 > 0 && rmsNow > 0.003f) effortSlow += (rmsNow - effortSlow) * (1.0f - std::exp(-blockSec / 2.0f));
    }
    profile.addAudio(m, n, voicedNow);
    {
        const juce::SpinLock::ScopedTryLockType lock(profileLock);
        if (lock.isLocked()) summaryCopy = profile.summary();
    }

    // ---- continuous real-time Auto-Match (converges within ~50-120 ms, even without UI timer) ----
    const auto shape = ageShape(param(ids::age));
    const int charIdx = juce::roundToInt(param(ids::character));
    const auto& charList = characters();
    const float fastMed = (voicedHopCount >= 6 && fastLogF0 > 0.0f) ? std::exp(fastLogF0) : 0.0f;
    const float spkF0 = summaryCopy.ready() ? summaryCopy.medianF0 : fastMed;

    float basePitchIn = param(ids::pitch);
    float baseFormantIn = param(ids::formant);
    if (param(ids::autoMatch) > 0.5f && charIdx > 0 && charIdx < (int) charList.size() && spkF0 > 40.0f && !userTookOver.load()) {
        const Character& curChar = charList[(size_t) charIdx];
        if (curChar.targetF0 > 0.0f) {
            const auto [idealPitchRaw, idealFormantRaw] = anchoredCharacterShift(curChar, charIdx, summaryCopy, spkF0);
            const float idealPitch = juce::jlimit(-18.0f, 18.0f, idealPitchRaw - shape.pitch);
            const float idealFormant = juce::jlimit(-8.0f, 8.0f, idealFormantRaw - shape.formant);
            const float autoAlpha = 1.0f - std::exp(-blockSec / 0.045f);
            activeAutoPitch += (idealPitch - activeAutoPitch) * autoAlpha;
            activeAutoFormant += (idealFormant - activeAutoFormant) * autoAlpha;
            basePitchIn = activeAutoPitch;
            baseFormantIn = activeAutoFormant;
            pendingAutoPitch.store(activeAutoPitch);
            pendingAutoFormant.store(activeAutoFormant);
            hasPendingAuto.store(true);
        }
    } else {
        activeAutoPitch = basePitchIn;
        activeAutoFormant = baseFormantIn;
    }

    // ---- dynamic prosody, vocal effort -> glottal press, jaw-opening formants & subharmonic/breath curves ----
    const float effortRatio = effortSlow > 1e-4f ? juce::jlimit(0.4f, 2.5f, effortFast / effortSlow) : 1.0f;
    const float effortLog = effortSlow > 1e-4f ? juce::jlimit(-1.0f, 1.0f, std::log2(effortRatio)) : 0.0f;
    const float excursionSt = (melodyFast > 0.0f && melodySlow > 0.0f) ? 12.0f * (melodyFast - melodySlow) / std::log(2.0f) : 0.0f;
    const bool voiceNow = melodyFast > 0 && melodySlow > 0 && fastF0 > 0;

    float dynPitch = 0.0f, dynFormant = 0.0f;
    if (voiceNow) {
        if (param(ids::autoSteady) > 0.5f) {
            dynPitch = juce::jlimit(-1.5f, 1.5f, -0.45f * excursionSt);
        } else if (charIdx == 5 || charIdx == 6 || charIdx == 7 || charIdx == 10 || charIdx == 12) {
            dynPitch = juce::jlimit(-2.2f, 2.2f, 0.24f * excursionSt);
        } else if (charIdx == 1 || charIdx == 2 || charIdx == 3) {
            dynPitch = juce::jlimit(-1.5f, 1.5f, -0.18f * excursionSt);
        }
        if (param(ids::autoTract) > 0.5f) {
            const float melShift = spkF0 > 40.0f ? 12.0f * (melodyFast - std::log(spkF0)) / std::log(2.0f) : excursionSt;
            const float jawOpen = 0.45f * std::max(0.0f, effortLog);
            dynFormant = juce::jlimit(-2.0f, 2.0f, 0.12f * melShift + jawOpen);
        }
    }

    float glottalDyn = 0.0f, growlDyn = 0.0f, breathDyn = 0.0f;
    if (param(ids::autoResponse) > 0.5f && effortSlow > 1e-4f) {
        glottalDyn = -0.20f * effortLog;
        const bool allowAutoFry = (param(ids::growl) > 0.02f) || (charIdx >= 1 && charIdx <= 4) || (charIdx >= 8);
        if (voiceNow && allowAutoFry)
            growlDyn = 0.12f * std::max(0.0f, effortLog) + 0.14f * std::clamp(-excursionSt / 3.0f, 0.0f, 1.0f);
        if (charIdx > 0 || param(ids::breath) > 0.01f)
            breathDyn = 0.12f * std::clamp(-effortLog - 0.08f, 0.0f, 1.0f);
    }

    float linkedFormantOffset = 0.0f;
    if (param(ids::follow) > 0.5f && getActiveEditor() == nullptr)
        linkedFormantOffset = (basePitchIn - lastLinkedPitch) * 0.45f;

    pitchSm.setTargetValue(basePitchIn + shape.pitch + dynPitch);
    formantSm.setTargetValue(juce::jlimit(-8.0f, 8.0f, baseFormantIn + linkedFormantOffset) + shape.formant + dynFormant);
    regenSm.setTargetValue(param(ids::regen));
    glottalSm.setTargetValue(juce::jlimit(-1.0f, 1.0f, param(ids::glottal) + glottalDyn));
    growlSm.setTargetValue(juce::jlimit(0.0f, 1.0f, param(ids::growl) + growlDyn));
    breathSm.setTargetValue(juce::jlimit(0.0f, 1.0f, param(ids::breath) + breathDyn));
    autotuneSm.setTargetValue(param(ids::autotune));
    for (auto* sm : {&pitchSm, &formantSm, &regenSm, &glottalSm, &growlSm, &breathSm, &autotuneSm}) sm->skip(n);

    // ---- Studio Auto-Tune (v3.0 melisma-aware + Retune Speed + Vibrato Preserve + Scale/Key Lock) ----
    const float basePitch = pitchSm.getCurrentValue();
    adub::AutoTune::Settings atSettings;
    atSettings.amount = autotuneSm.getCurrentValue();
    atSettings.retuneSpeedMs = param(ids::retuneSpeedMs);
    atSettings.vibratoPreserve = param(ids::vibratoPreserve);
    atSettings.scaleMode = static_cast<adub::AutoTune::ScaleMode>(juce::jlimit(0, 5, juce::roundToInt(param(ids::scaleMode))));
    atSettings.manualKey = juce::jlimit(0, 11, juce::roundToInt(param(ids::manualKey)));

    const float tuneF0 = fastF0 > 0.0f ? fastF0 : (useNeural ? neuralF0 : 0.0f);
    const float tuneConf = useNeural ? std::max(neuralConf, fastConf * 0.85f) : fastConf;
    const auto tuneRes = autoTune.update(tuneF0, tuneConf, basePitch, atSettings, summaryCopy.type, blockSec);
    liveKey.store(tuneRes.key);
    liveScale.store((int) tuneRes.scale);
    liveTuneCents.store(tuneRes.correctionSemitones * 100.0f);

    const float p = juce::jlimit(-20.0f, 20.0f, basePitch + tuneRes.correctionSemitones);
    const float f = formantSm.getCurrentValue();

    adub::HybridParams hp;
    hp.pitchSemitones = p;
    hp.formantSemitones = f;
    hp.regen = regenSm.getCurrentValue();
    hp.glottal = glottalSm.getCurrentValue();
    hp.growl = growlSm.getCurrentValue();
    hp.breath = breathSm.getCurrentValue();
    hp.age = param(ids::age);
    hp.nasal = param(ids::nasal);
    hp.deEsser = param(ids::deEsser);
    hp.effort = effortRatio;
    hp.speakerMedianF0 = spkF0;
    hp.characterIndex = charIdx;
    hp.consonantGuard = param(ids::consonantGuard) > 0.5f;
    hp.steadyPitch = param(ids::autoSteady) > 0.5f;

    regenerator.process(rawDry.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(), n, hp, useNeural);
    const auto st = regenerator.getStatus();
    liveVoiced.store(st.voiced);
    liveConsonant.store(st.consonant);
    liveRegen.store(st.regen);

    juce::dsp::AudioBlock<float> wet(buffer);

    // ---- 4-band studio vocal EQ ----
    float bodyAuto = 0, airAuto = shape.air;
    if (param(ids::autoTone) > 0.5f) {
        bodyAuto = juce::jlimit(-1.5f, 2.5f, -0.12f * p);
        airAuto += juce::jlimit(-1.5f, 1.5f, 0.03f * p - 0.15f * f);
    }
    float presenceDyn = 0;
    if (charIdx > 0 && param(ids::autoResponse) > 0.5f && effortSlow > 1e-4f) presenceDyn = 2.0f * effortLog;
    lastPresenceDyn += (presenceDyn - lastPresenceDyn) * 0.25f;
    updateFilters(bodyAuto, airAuto, lastPresenceDyn);
    if (lastNasal > 0.001f) {
        nasalPeak.process(juce::dsp::ProcessContextReplacing<float>(wet));
        nasalLow.process(juce::dsp::ProcessContextReplacing<float>(wet));
    }
    if (std::abs(lastBody) > 0.05f) bodyEq.process(juce::dsp::ProcessContextReplacing<float>(wet));
    if (std::abs(lastPresence) > 0.05f) presenceEq.process(juce::dsp::ProcessContextReplacing<float>(wet));
    if (std::abs(lastAir) > 0.05f) airEq.process(juce::dsp::ProcessContextReplacing<float>(wet));

    // ---- ITU-R BS.1770 K-Weighted Gated Loudness Auto-Match (-48 dBFS gate, zero breath pumping) ----
    if (param(ids::loudness) > 0.5f) {
        for (int i = 0; i < n; ++i) {
            float dMono = 0.0f, wMono = 0.0f;
            for (int c = 0; c < ch; ++c) {
                dMono += dry.getSample(c, i);
                wMono += buffer.getSample(c, i);
            }
            dMono /= (float) ch;
            wMono /= (float) ch;
            const float kGain = loudnessMatcher.step(dMono, wMono);
            for (int c = 0; c < ch; ++c) buffer.setSample(c, i, buffer.getSample(c, i) * kGain);
        }
    }

    // ---- Dry/Wet Mix + Output Gain + 3 ms Lookahead -0.3 dBFS True-Peak Limiter ----
    mixSm.setTargetValue(param(ids::mix));
    gainSm.setTargetValue(juce::Decibels::decibelsToGain(param(ids::output)));
    float outPeak = 0.0f;
    float signedSum = 0.0f;
    float frameSamples[8]{};
    const int nCh = std::min(ch, 8);
    for (int i = 0; i < n; ++i) {
        const float mx = mixSm.getNextValue(), g = gainSm.getNextValue();
        for (int c = 0; c < nCh; ++c) {
            float v = (buffer.getSample(c, i) * mx + dry.getSample(c, i) * (1.0f - mx)) * g;
            if (!std::isfinite(v)) v = 0.0f;
            frameSamples[c] = v;
        }
        const float pk = limiter.processFrame(frameSamples, nCh);
        float monoOut = 0.0f;
        for (int c = 0; c < nCh; ++c) {
            buffer.setSample(c, i, frameSamples[c]);
            monoOut += frameSamples[c];
        }
        monoOut /= (float) nCh;
        outPeak = std::max(outPeak, pk);
        signedSum += monoOut;
    }
    outputLevel.store(outPeak);
    liveLimiterGrDb.store(limiter.consumeMaxGrDb());

    // Push block summary (signed peak-weighted displacement) into 64-slot ring for the reactive UI wave
    const float signedAvg = std::tanh(3.5f * (signedSum / (float) n));
    const float waveSample = (outPeak > 0.002f ? (signedAvg >= 0 ? outPeak : -outPeak) : 0.0f);
    const int wp = waveWritePos.load(std::memory_order_relaxed);
    waveHistory[(size_t) (wp & 63)].store(waveSample, std::memory_order_relaxed);
    waveWritePos.store(wp + 1, std::memory_order_release);
}

void AlptugDUBProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void AlptugDUBProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(state.state.getType())) {
            suppressLink.store(true);
            state.replaceState(juce::ValueTree::fromXml(*xml));
            activeAutoPitch = lastAutoSetPitch = lastLinkedPitch = param(ids::pitch);
            activeAutoFormant = lastAutoSetFormant = param(ids::formant);
            suppressLink.store(false);
        }
    }
}

juce::AudioProcessorEditor* AlptugDUBProcessor::createEditor() { return new AlptugDUBEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AlptugDUBProcessor(); }
