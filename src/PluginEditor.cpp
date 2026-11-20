// AlptugDUB by Alptuğ Dağ — v3.0 Liquid Glass + Walnut Wood + Tactile Analog Console Editor.
#include "PluginEditor.h"

#include <BinaryData.h>

using namespace juce;

namespace {
bool turkish() {
    static const bool tr = SystemStats::getUserLanguage().startsWithIgnoreCase("tr") ||
                           SystemStats::getDisplayLanguage().startsWithIgnoreCase("tr");
    return tr;
}
String T(const char* tr, const char* en) { return String::fromUTF8(turkish() ? tr : en); }
const String authorName = String::fromUTF8("Alptuğ Dağ");

const char* typeName(adub::VoiceType t) {
    const bool tr = turkish();
    switch (t) {
        case adub::VoiceType::Bass: return tr ? "Bas" : "Bass";
        case adub::VoiceType::Baritone: return tr ? "Bariton" : "Baritone";
        case adub::VoiceType::Tenor: return "Tenor";
        case adub::VoiceType::Alto: return "Alto";
        case adub::VoiceType::MezzoSoprano: return tr ? "Mezzosoprano" : "Mezzo-soprano";
        case adub::VoiceType::Soprano: return "Soprano";
        case adub::VoiceType::Child: return tr ? "Çocuk" : "Child";
        case adub::VoiceType::Unknown:
        default: return "—";
    }
}

String keyScaleLabel(int key, int scale) {
    static constexpr const char* kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (scale == 2) return T("Kromatik", "Chromatic");
    const String root = kNotes[(key % 12 + 12) % 12];
    switch (scale) {
        case 0: return root + " Maj";
        case 1: return root + " Min";
        case 3: return root + T(" Hicaz", " Hijaz");
        case 4: return root + T(" Pent.", " Pent.");
        default: return T("Kromatik", "Chromatic");
    }
}

float meterPosition(float level) { return jlimit(0.0f, 1.0f, (Decibels::gainToDecibels(level, -60.0f) + 60.0f) / 60.0f); }

std::unique_ptr<Drawable> signatureIn(Colour c) {
    auto d = Drawable::createFromImageData(BinaryData::signature_svg, BinaryData::signature_svgSize);
    if (d) d->replaceColour(Colours::white, c);
    return d;
}
} // namespace

// ---- Knob -------------------------------------------------------------------------------------------

Knob::Knob(AudioProcessorValueTreeState& s, const char* id, const String& name, const String& tip, bool big) : title(name), large(big) {
    slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(Slider::NoTextBox, true, 0, 0);
    slider.setTooltip(tip);
    slider.setRotaryParameters(MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity(big ? 320 : 220);
    slider.setVelocityBasedMode(false);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible(slider);
    attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, id, slider);
    slider.setDoubleClickReturnValue(true, (double) s.getParameter(id)->convertFrom0to1(s.getParameter(id)->getDefaultValue()));
}

void Knob::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(large ? 24 : 18);
    r.removeFromBottom(large ? 32 : 24);
    slider.setBounds(r);
}

void Knob::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat();
    ui::caption(g, title, r.removeFromTop(large ? 20.0f : 15.0f), Justification::centred, large ? 12.5f : 10.0f);

    // Liquid Glass numeric readout pill beneath each analog knob
    auto valBox = r.removeFromBottom(large ? 26.0f : 20.0f).withSizeKeepingCentre(large ? 92.0f : 76.0f, large ? 22.0f : 18.0f);
    g.setColour(ui::colours::well.withAlpha(0.85f));
    g.fillRoundedRectangle(valBox, 6.0f);
    g.setColour(Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(valBox, 6.0f, 0.8f);
    g.setColour(large ? ui::colours::gold : ui::colours::ink);
    g.setFont(ui::font(large ? 13.5f : 11.0f, 600));
    g.drawText(slider.getTextFromValue(slider.getValue()), valBox, Justification::centred);
}

// ---- FineTuneDrawer ---------------------------------------------------------------------------------

FineTuneDrawer::FineTuneDrawer(AlptugDUBProcessor& proc, const String& closeText)
    : growl(proc.state, ids::growl, T("Hırıltı", "Growl"),
            T("Ses teli alt harmonikleri (F0/2, F0/3) ve gırtlak hırıltısı", "Subharmonic vocal fry (F0/2, F0/3) and throat rasp")),
      breath(proc.state, ids::breath, T("Nefes", "Breath"),
             T("Perdeyle eşzamanlı, formantla şekillenmiş doğal nefes dokusu", "Pitch-synchronous, formant-shaped glottal aspiration")),
      age(proc.state, ids::age, T("Yaş", "Age"),
          T("Eksi: daha genç ve parlak. Artı: daha yaşlı ve titrek", "Minus: younger and brighter. Plus: older with subtle tremor")),
      nasal(proc.state, ids::nasal, T("Geniz", "Nasal"), T("Burun / geniz rezonansı (Cadı, Goblin)", "Nasal cavity resonance")),
      body(proc.state, ids::body, T("Gövde", "Body"), T("Alt frekans göğüs sıcaklığı", "Low-end chest warmth")),
      air(proc.state, ids::air, T("Hava", "Air"), T("Üst frekans nefes parlaklığı", "High-frequency silky air")),
      presence(proc.state, ids::presence, T("Netlik", "Presence"), T("Konuşma anlaşılırlığı ve odak", "Vocal presence & clarity")),
      output(proc.state, ids::output, T("Çıkış", "Output"), T("Çıkış seviyesi (dB)", "Output level (dB)")),
      hpf(proc.state, ids::hpfHz, "HPF",
          T("2. derece Butterworth yüksek geçiren filtre: <40-160 Hz mikrofon uğultusunu temizler",
            "2nd-order Butterworth HPF: removes <40-160 Hz mic rumble before pitch analysis")),
      gate(proc.state, ids::gateDb, T("Kapı", "Gate"),
           T("Yumuşak gürültü kapısı (-72 dB = kapalı)", "Smooth studio noise gate (-72 dB = off)")),
      deEsser(proc.state, ids::deEsser, "De-Esser",
              T("S/Ş/Ç ıslık frekanslarını (5.2–10.5 kHz) dinamik olarak yumuşatır",
                "Dynamically tames harsh sibilants (5.2-10.5 kHz) on detected consonants")),
      retuneSpeed(proc.state, ids::retuneSpeedMs, T("Tepki", "Retune"),
                  T("Auto-Tune perde yakalama hızı (5 ms sıkı, 120 ms doğal)", "Auto-Tune retune time constant (5 ms tight, 120 ms natural)")),
      vibratoPreserve(proc.state, ids::vibratoPreserve, "Vibrato",
                      T("Auto-Tune açıkken doğal insan vibratosunu koruma oranı", "Preserves natural human vibrato while Auto-Tune is active")) {
    for (auto* k : {&growl, &breath, &age, &nasal, &body, &air, &presence, &output,
                    &hpf, &gate, &deEsser, &retuneSpeed, &vibratoPreserve})
        addAndMakeVisible(k);

    scaleBox.addItem(T("Otomatik (AI)", "Auto (AI)"), 1);
    scaleBox.addItem(T("Majör", "Major"), 2);
    scaleBox.addItem(T("Minör", "Minor"), 3);
    scaleBox.addItem(T("Kromatik", "Chromatic"), 4);
    scaleBox.addItem(T("Hicaz", "Hijaz"), 5);
    scaleBox.addItem(T("Pentatonik", "Pentatonic"), 6);
    scaleBox.setTooltip(T("Auto-Tune gam kilidi veya Krumhansl-Schmuckler otomatik algılama",
                          "Auto-Tune scale lock or Krumhansl-Schmuckler auto detection"));
    scaleBox.onChange = [this] { syncKeyEnabled(); };
    addAndMakeVisible(scaleBox);
    scaleAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state, ids::scaleMode, scaleBox);

    static constexpr const char* kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (int i = 0; i < 12; ++i) keyBox.addItem(kNotes[i], i + 1);
    keyBox.setTooltip(T("Manuel gam modu seçiliyken kök nota (C..B)", "Root key (C..B) when manual scale lock is selected"));
    addAndMakeVisible(keyBox);
    keyAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state, ids::manualKey, keyBox);
    syncKeyEnabled();

    const char* sw[][5] = {
        {ids::autoTract, "Doğal gırtlak hareketi", "Natural tract motion",
         "Perde yükselip alçaldıkça gırtlak da hafifçe hareket eder (en çok ±2 yarım ton).",
         "The vocal tract rises and falls with the melody, as a real voice does (at most +-2 semitones)."},
        {ids::autoResponse, "Dinamik tepki", "Dynamic response",
         "Ton ve hırıltı sesin şiddetini izler: bağırınca daha belirgin, kısılınca daha sıcak.",
         "Tone and growl follow vocal effort: brighter and grittier when pushed, softer when eased off."},
        {ids::autoSteady, "Titreşimi yumuşat", "Steady pitch",
         "Perdedeki titrekliği ve ani oynamaları yumuşatır: daha kontrollü, oturaklı bir ses.",
         "Calms pitch wobble and sudden wanders: a steadier, more controlled voice."},
        {ids::autoTone, "Akıllı ton dengesi", "Smart tone",
         "Sinirsel tını modeliyle birlikte ton dengesini ince ayarlar.", "Fine-tunes spectral tilt alongside the neural Bark timbre shaper."},
    };
    for (auto& d : sw) {
        auto* b = switches.add(new ToggleButton(T(d[1], d[2])));
        b->setTooltip(T(d[3], d[4]));
        addAndMakeVisible(b);
        switchAttachments.add(new AudioProcessorValueTreeState::ButtonAttachment(proc.state, d[0], *b));
    }

    closeBtn.setButtonText(closeText);
    closeBtn.onClick = [this] {
        if (onClose) onClose();
    };
    addAndMakeVisible(closeBtn);
}

void FineTuneDrawer::syncKeyEnabled() {
    const int id = scaleBox.getSelectedId();
    // Enable manual Key selector whenever a pitched manual scale (Major/Minor/Hijaz/Pentatonic) is active
    keyBox.setEnabled(id == 2 || id == 3 || id == 5 || id == 6);
    keyBox.setAlpha(keyBox.isEnabled() ? 1.0f : 0.48f);
}

void FineTuneDrawer::resized() {
    auto card = getLocalBounds().withSizeKeepingCentre(1040, 516);
    closeBtn.setBounds(card.getRight() - 108, card.getY() + 16, 86, 32);

    Knob* row1[] = {&growl, &breath, &age, &nasal, &body, &air, &presence, &output};
    for (int i = 0; i < 8; ++i) row1[i]->setBounds(card.getX() + 24 + i * 124, card.getY() + 62, 120, 130);

    Knob* row2[] = {&hpf, &gate, &deEsser, &retuneSpeed, &vibratoPreserve};
    for (int i = 0; i < 5; ++i) row2[i]->setBounds(card.getX() + 24 + i * 124, card.getY() + 238, 120, 130);

    scaleBox.setBounds(card.getX() + 676, card.getY() + 276, 206, 36);
    keyBox.setBounds(card.getX() + 894, card.getY() + 276, 118, 36);

    const int swX = card.getX() + 34, swY = card.getY() + 402;
    for (int i = 0; i < switches.size(); ++i) switches[i]->setBounds(swX + (i % 2) * 490, swY + (i / 2) * 40, 464, 32);
}

void FineTuneDrawer::paint(Graphics& g) {
    g.fillAll(Colours::black.withAlpha(0.68f));
    auto card = getLocalBounds().withSizeKeepingCentre(1040, 516).toFloat();
    ui::art::glassPanel(g, card, 18.0f, 0.92f);
    ui::caption(g, T("İnce Ayar · Doku, Stüdyo Ön İşleme ve Auto-Tune", "Fine Tune · Texture, Studio Conditioning & Auto-Tune"),
                Rectangle<float>(card.getX() + 28, card.getY() + 20, 700, 26), Justification::centredLeft, 12.0f, ui::colours::amber);

    g.setColour(Colours::white.withAlpha(0.08f));
    g.drawHorizontalLine((int) card.getY() + 204, card.getX() + 28, card.getRight() - 28);

    ui::caption(g, T("Stüdyo Ön İşleme & Auto-Tune 3.0", "Studio Conditioning & Auto-Tune 3.0"),
                Rectangle<float>(card.getX() + 28, card.getY() + 214, 520, 16), Justification::centredLeft, 10.0f, ui::colours::cyan.withAlpha(0.85f));

    ui::caption(g, T("Gam Kilidi", "Scale Lock"), Rectangle<float>(card.getX() + 676, card.getY() + 252, 206, 16), Justification::centredLeft, 10.0f);
    ui::caption(g, T("Kök Ton", "Root Key"), Rectangle<float>(card.getX() + 894, card.getY() + 252, 118, 16), Justification::centredLeft, 10.0f);

    g.setColour(ui::colours::faint);
    g.setFont(ui::font(11.0f, 500));
    g.drawFittedText(T("Otomatik mod Krumhansl-Schmuckler ile gamı sesinizden öğrenir; manuel modda Majör, Minör, Hicaz veya Pentatonik kilitlenir.",
                       "Auto mode learns key & scale via Krumhansl-Schmuckler; manual mode locks Major, Minor, Hijaz, or Pentatonic."),
                     Rectangle<int>((int) card.getX() + 676, (int) card.getY() + 322, 336, 44), Justification::topLeft, 3);

    g.setColour(Colours::white.withAlpha(0.08f));
    g.drawHorizontalLine((int) card.getY() + 384, card.getX() + 28, card.getRight() - 28);
}

// ---- About ------------------------------------------------------------------------------------------

AboutPage::AboutPage() {
    photo = ImageCache::getFromMemory(BinaryData::creator_jpg, BinaryData::creator_jpgSize);
    signature = signatureIn(ui::colours::ink);
    const char* items[][2] = {{"Instagram  @alptugdag", "https://instagram.com/alptugdag"},
                              {"YouTube  @AlptugDag", "https://youtube.com/@AlptugDag"},
                              {"Letterboxd", "https://letterboxd.com/AlptugDag/"},
                              {"LinkedIn", "https://www.linkedin.com/in/AlptugDag/"},
                              {"GitHub", "https://github.com/AlptugDag"},
                              {"CineTV", "https://github.com/AlptugDag/cinetv-launcher"}};
    for (auto& it : items) {
        auto* b = links.add(new TextButton(String::fromUTF8(it[0])));
        const String url = it[1];
        b->setTooltip(url);
        b->onClick = [url] { URL(url).launchInDefaultBrowser(); };
        addAndMakeVisible(b);
    }
    close.setButtonText(T("Kapat", "Close"));
    close.onClick = [this] { setVisible(false); };
    addAndMakeVisible(close);
}

void AboutPage::resized() {
    auto card = getLocalBounds().withSizeKeepingCentre(820, 470);
    auto right = card.withTrimmedLeft(330).reduced(34, 0);
    const int y = card.getY() + 262;
    for (int i = 0; i < links.size(); ++i) links[i]->setBounds(right.getX() + (i % 2) * 210, y + (i / 2) * 46, 200, 38);
    close.setBounds(right.getRight() - 120, card.getBottom() - 60, 120, 38);
}

void AboutPage::paint(Graphics& g) {
    g.fillAll(Colours::black.withAlpha(0.72f));
    auto card = getLocalBounds().withSizeKeepingCentre(820, 470).toFloat();
    ui::art::glassPanel(g, card, 18.0f, 0.92f);
    auto pic = Rectangle<float>(card.getX() + 34, card.getY() + 34, 262, 330);
    ui::art::photo(g, photo, pic, 12);
    if (signature)
        signature->drawWithin(g, Rectangle<float>(pic.getX() + 50, pic.getBottom() + 8, pic.getWidth() - 100, 90), RectanglePlacement::centred, 0.95f);

    auto text = card.withTrimmedLeft(330).reduced(34, 40);
    g.setColour(ui::colours::ink);
    g.setFont(ui::font(36, 900));
    g.drawText(authorName, text.removeFromTop(48), Justification::left);
    ui::caption(g, T("Sinemacı · Hikâye anlatıcı · Seslendirme sanatçısı", "Filmmaker · Storyteller · Voice artist"), text.removeFromTop(24),
                Justification::left, 11.5f, ui::colours::amber);
    text.removeFromTop(10);
    g.setColour(ui::colours::dim);
    g.setFont(ui::font(15, 500));
    g.drawFittedText(T("2000'de Ankara'da doğdu. Yönettiği kısa film Sessiz (2023) ile tanınır. AlptugDUB'ı kendi "
                       "seslendirme ve dublaj işleri için tasarlayıp geliştirdi.",
                       "Born in Ankara in 2000, known for his short film Sessiz (2023). He designed and built AlptugDUB for "
                       "his own voice-over and dubbing work."),
                     text.removeFromTop(120).toNearestInt(), Justification::topLeft, 5);
    g.setColour(ui::colours::faint);
    g.setFont(ui::font(12, 500));
    g.drawText(String::fromUTF8("AlptugDUB " JucePlugin_VersionString "  ·  AGPL-3.0  ·  github.com/AlptugDag/AlptugDUB"),
               Rectangle<float>(text.getX(), card.getBottom() - 52, 420, 20), Justification::left);
}

// ---- Editor -----------------------------------------------------------------------------------------

AlptugDUBEditor::AlptugDUBEditor(AlptugDUBProcessor& p)
    : AudioProcessorEditor(p), proc(p),
      regen(p.state, ids::regen, T("Üretim", "Regen"),
            T("Hibrit yeniden üretim oranı: %0 spektral, %100 tam LF glottal sentez",
              "Hybrid regeneration mix: 0% spectral only, 100% full LF glottal resynthesis")),
      autotune(p.state, ids::autotune, "Auto-Tune",
               T("İnsan sesi için otomatik gam algılayan ve vibratoyu koruyan akıllı perde düzeltici",
                 "Human-voice optimized adaptive Auto-Tune with automatic key detection")),
      pitch(p.state, ids::pitch, T("Perde", "Pitch"), T("Sesin perdesi, yarım ton. Çift tık: sıfırla", "Voice pitch in semitones. Double-click: reset"), true),
      formant(p.state, ids::formant, T("Gırtlak", "Formant"),
              T("Doğrusal olmayan F1/F2/F3 ses yolu bükme: aşağısı iri, yukarısı küçük beden",
                "Non-linear F1/F2/F3 vocal tract warp: down is a bigger body, up a smaller one"),
              true),
      glottal(p.state, ids::glottal, T("Doku", "Glottal"),
              T("Eksi: güçlü göğüs sesi. Artı: yumuşak, havalı kafa sesi", "Minus: pressed chest voice. Plus: soft, airy head voice")),
      mix(p.state, ids::mix, T("Karışım", "Mix"), T("İşlenmiş ve orijinal ses", "Processed against original")),
      fineTuneDrawer(p, T("Kapat", "Close")) {
    ui::setTurkish(turkish());
    setLookAndFeel(&look);
    wood = ImageCache::getFromMemory(BinaryData::wood_jpg, BinaryData::wood_jpgSize);
    signature = signatureIn(ui::colours::ink);
    addAndMakeVisible(content);
    content.setSize(W, H);
    content.draw = [this](Graphics& g) { drawStage(g); };

    for (auto* k : {&regen, &autotune, &pitch, &formant, &glottal, &mix}) content.addAndMakeVisible(k);

    // Tactile jewel-LED analog console pushbuttons
    aiBtn.setButtonText(T("Yapay Zekâ", "AI Pitch"));
    aiBtn.getProperties().set("analog", true);
    aiBtn.getProperties().set("cyanLed", true);
    aiBtn.setTooltip(T("SwiftF0 yapay zekâ perde ve gam analizi", "SwiftF0 neural pitch & key analysis"));
    aiAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state, ids::ai, aiBtn);

    guardBtn.setButtonText(T("Ünsüz Koruma", "Consonant"));
    guardBtn.getProperties().set("analog", true);
    guardBtn.setTooltip(T("S, Ş, Ç, T, K gibi ünsüzleri bozmadan korur", "Preserves sibilants and plosives unshifted"));
    guardAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state, ids::consonantGuard, guardBtn);

    linkBtn.setButtonText(T("Bağla", "Link"));
    linkBtn.getProperties().set("analog", true);
    linkBtn.getProperties().set("cyanLed", true);
    linkBtn.setTooltip(T("Perde ve gırtlağı orantılı biçimde birbirine bağlar", "Links pitch and formant proportionally"));
    linkAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state, ids::follow, linkBtn);

    prevPitchSlider = (float) pitch.slider.getValue();
    prevFormantSlider = (float) formant.slider.getValue();
    linkBtn.onClick = [this] {
        if (linkBtn.getToggleState() && std::abs(formant.slider.getValue()) < 0.05 && std::abs(pitch.slider.getValue()) > 0.1) {
            syncingLink = true;
            formant.slider.setValue(jlimit(-8.0, 8.0, pitch.slider.getValue() * 0.45), sendNotificationSync);
            syncingLink = false;
        }
        prevPitchSlider = (float) pitch.slider.getValue();
        prevFormantSlider = (float) formant.slider.getValue();
    };
    pitch.slider.onValueChange = [this] {
        pitch.repaint();
        if (!syncingLink && !proc.isAutoUpdatingParams() && linkBtn.getToggleState()) {
            const float dp = (float) pitch.slider.getValue() - prevPitchSlider;
            if (std::abs(dp) > 1e-4f) {
                syncingLink = true;
                formant.slider.setValue(jlimit(-8.0, 8.0, formant.slider.getValue() + dp * 0.45), sendNotificationSync);
                syncingLink = false;
            }
        }
        prevPitchSlider = (float) pitch.slider.getValue();
        prevFormantSlider = (float) formant.slider.getValue();
    };
    formant.slider.onValueChange = [this] {
        formant.repaint();
        if (!syncingLink && !proc.isAutoUpdatingParams() && linkBtn.getToggleState()) {
            const float df = (float) formant.slider.getValue() - prevFormantSlider;
            if (std::abs(df) > 1e-4f) {
                syncingLink = true;
                pitch.slider.setValue(jlimit(-18.0, 18.0, pitch.slider.getValue() + df / 0.45), sendNotificationSync);
                syncingLink = false;
            }
        }
        prevPitchSlider = (float) pitch.slider.getValue();
        prevFormantSlider = (float) formant.slider.getValue();
    };

    autoMatchBtn.setButtonText(T("Sesime Uyarla", "Auto-Match"));
    autoMatchBtn.getProperties().set("analog", true);
    autoMatchBtn.setTooltip(T("Karakteri sesinize göre otomatik ayarlar", "Auto-tunes character pitch & tract to your voice"));
    autoMatchAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state, ids::autoMatch, autoMatchBtn);

    loudnessBtn.setButtonText(T("Seviye Koru", "Keep Level"));
    loudnessBtn.getProperties().set("analog", true);
    loudnessBtn.setTooltip(T("ITU-R BS.1770 K-ağırlıklı eşleştirme ile ses şiddeti korunur",
                             "Matches loudness using ITU-R BS.1770 K-weighted gated metering"));
    loudnessAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state, ids::loudness, loudnessBtn);

    for (auto* tb : {&aiBtn, &guardBtn, &linkBtn, &autoMatchBtn, &loudnessBtn}) content.addAndMakeVisible(tb);

    relearnBtn.setButtonText(String::fromUTF8("↺"));
    relearnBtn.setTooltip(T("Sesimi ve gamı yeniden öğren", "Relearn my voice & key"));
    relearnBtn.onClick = [this] { proc.resetVoiceProfile(); };

    fineTuneBtn.setButtonText(T("İnce Ayar", "Fine Tune"));
    fineTuneBtn.onClick = [this] { toggleFineTune(); };

    aboutBtn.setButtonText(T("Hakkında", "About"));
    aboutBtn.onClick = [this] {
        fineTuneDrawer.setVisible(false);
        fineTuneBtn.setToggleState(false, dontSendNotification);
        about.setVisible(true);
        about.toFront(false);
    };
    for (auto* b : {&relearnBtn, &fineTuneBtn, &aboutBtn}) content.addAndMakeVisible(b);

    // Physical analog character key bank
    const auto& chars = AlptugDUBProcessor::characters();
    for (int i = 0; i < (int) chars.size(); ++i) {
        auto* b = characterButtons.add(new TextButton(String::fromUTF8(turkish() ? chars[(size_t) i].nameTr : chars[(size_t) i].nameEn)));
        b->getProperties().set("segment", true);
        b->onClick = [this, i] { proc.applyCharacter(i); };
        content.addAndMakeVisible(b);
    }

    fineTuneDrawer.setComponentID("finetune");
    fineTuneDrawer.onClose = [this] {
        fineTuneDrawer.setVisible(false);
        fineTuneBtn.setToggleState(false, dontSendNotification);
    };
    content.addChildComponent(fineTuneDrawer);
    about.setComponentID("about");
    content.addChildComponent(about);

    layoutContent();
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double) W / H);
    setResizeLimits(W * 3 / 5, H * 3 / 5, W * 2, H * 2);
    setSize(W * 9 / 10, H * 9 / 10);
    timerCallback();
    startTimerHz(30);
}

AlptugDUBEditor::~AlptugDUBEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void AlptugDUBEditor::resized() { content.setTransform(AffineTransform::scale((float) getWidth() / W, (float) getHeight() / H)); }

int AlptugDUBEditor::currentCharacter() const { return roundToInt(proc.state.getRawParameterValue(ids::character)->load()); }

void AlptugDUBEditor::toggleFineTune() {
    const bool open = !fineTuneDrawer.isVisible();
    about.setVisible(false);
    fineTuneDrawer.setVisible(open);
    fineTuneBtn.setToggleState(open, dontSendNotification);
    if (open) fineTuneDrawer.toFront(false);
}

void AlptugDUBEditor::layoutContent() {
    // Walnut wood cheeks: 0..32 and 1168..1200. Inner chassis: 32..1168.
    headerArea = {54, 14, 1092, 52};
    relearnBtn.setBounds(758, 23, 36, 34);
    fineTuneBtn.setBounds(930, 23, 108, 34);
    aboutBtn.setBounds(1046, 23, 86, 34);

    // Curved Liquid Glass oscilloscope wave display
    lensArea = {54, 78, 1092, 216};

    // Symmetrical 3-module analog console deck (6 knobs total on main view)
    leftModArea = {54, 308, 296, 266};
    regen.setBounds(68, 340, 126, 158);
    autotune.setBounds(210, 340, 126, 158);
    aiBtn.setBounds(70, 514, 124, 42);
    guardBtn.setBounds(210, 514, 124, 42);

    centerModArea = {366, 308, 468, 266};
    pitch.setBounds(380, 336, 178, 218);
    linkBtn.setBounds(562, 422, 76, 38);
    formant.setBounds(642, 336, 178, 218);

    rightModArea = {850, 308, 296, 266};
    glottal.setBounds(864, 340, 126, 158);
    mix.setBounds(1006, 340, 126, 158);
    autoMatchBtn.setBounds(866, 514, 124, 42);
    loudnessBtn.setBounds(1006, 514, 124, 42);

    // Bottom physical analog character key bank (2 rows)
    charsArea = {54, 588, 1092, 114};
    const int n = characterButtons.size(), perRow = (n + 1) / 2;
    for (int i = 0; i < n; ++i) {
        const int row = i / perRow, col = i % perRow, inRow = row == 0 ? perRow : n - perRow;
        const float w = (charsArea.getWidth() - 20.0f) / (float) inRow;
        characterButtons[i]->setBounds(
            Rectangle<float>(charsArea.getX() + 10.0f + w * (float) col, charsArea.getY() + 12.0f + 46.0f * (float) row, w, 44.0f)
                .reduced(3.0f, 2.0f)
                .toNearestInt());
    }

    fineTuneDrawer.setBounds(0, 0, W, H);
    about.setBounds(0, 0, W, H);
}

void AlptugDUBEditor::drawGlassWave(Graphics& g, Rectangle<float> lens) {
    using namespace ui;
    art::glassLens(g, lens, 18.0f);

    // Subtle analog oscilloscope calibration grid inside the curved glass
    g.saveState();
    Path clip;
    clip.addRoundedRectangle(lens.reduced(2.0f), 16.0f);
    g.reduceClipRegion(clip);

    g.setColour(Colours::white.withAlpha(0.03f));
    for (int i = 1; i <= 5; ++i) {
        const float x = lens.getX() + lens.getWidth() * (float) i / 6.0f;
        g.drawVerticalLine((int) x, lens.getY() + 34.0f, lens.getBottom() - 32.0f);
    }
    const float cy = lens.getCentreY() - 2.0f;
    g.drawHorizontalLine((int) cy, lens.getX() + 24.0f, lens.getRight() - 24.0f);

    // Warm amber-to-cyan radial back-illumination proportional to live voice energy
    ColourGradient glowBg(colours::amber.withAlpha(0.11f + 0.14f * waveAmpSmooth), lens.getCentreX(), cy, Colours::transparentBlack,
                          lens.getCentreX() + 360.0f, cy + 120.0f, true);
    g.setGradientFill(glowBg);
    g.fillRect(lens);

    // Semi-transparent signature watermark behind the voice-reactive wave ribbons
    if (signature)
        signature->drawWithin(g, lens.withSizeKeepingCentre(310.0f, 164.0f), RectanglePlacement::centred, 0.11f);

    // Smooth the 64-slot audio displacement ring for silky organic ribbons
    std::array<float, 64> smoothEnv{};
    for (int i = 0; i < 64; ++i) {
        float acc = 0.0f, wsum = 0.0f;
        for (int d = -4; d <= 4; ++d) {
            const float w = std::exp(-0.18f * (float) (d * d));
            acc += std::abs(waveRing[(size_t) jlimit(0, 63, i + d)]) * w;
            wsum += w;
        }
        smoothEnv[(size_t) i] = acc / wsum;
    }

    const float leftX = lens.getX() + 44.0f;
    const float width = lens.getWidth() - 88.0f;
    const float baseAmp = 10.0f + 58.0f * waveAmpSmooth;
    const float pitchNorm = jlimit(0.65f, 2.1f, (liveF0 > 40.0f ? liveF0 : 140.0f) / 140.0f);
    const float harmFactor = jlimit(0.75f, 1.85f,
                                     1.0f + 0.025f * proc.state.getRawParameterValue(ids::pitch)->load() +
                                         0.035f * proc.state.getRawParameterValue(ids::formant)->load());

    struct Ribbon {
        float freq, phaseMul, speed, ampScale, thickness;
        Colour col;
    };
    const Ribbon ribbons[4] = {
        {2.4f * harmFactor, 0.00f, 1.00f, 0.90f, 2.3f, colours::gold},
        {3.3f * harmFactor, 1.25f, 1.24f, 1.00f, 1.9f, colours::amber},
        {4.2f * harmFactor, 2.45f, 0.85f, 0.76f, 1.6f, colours::cyan},
        {1.7f * harmFactor, 3.80f, 0.64f, 0.58f, 1.3f, colours::cyan.interpolatedWith(colours::ink, 0.35f)},
    };

    const int pts = 180;
    for (int r = 3; r >= 0; --r) {
        const auto& rb = ribbons[r];
        Path p;
        for (int i = 0; i <= pts; ++i) {
            const float u = (float) i / (float) pts;
            const float win = std::pow(std::sin(MathConstants<float>::pi * u), 1.9f);
            const float ringPos = u * 63.0f;
            const int i0 = jlimit(0, 63, (int) ringPos);
            const int i1 = jmin(63, i0 + 1);
            const float frac = ringPos - (float) i0;
            const float envVal = smoothEnv[(size_t) i0] + (smoothEnv[(size_t) i1] - smoothEnv[(size_t) i0]) * frac;
            const float envMod = 0.40f + 0.95f * jlimit(0.0f, 1.0f, envVal * 1.45f);

            const float ph = wavePhase * rb.speed + rb.phaseMul;
            const float carrier = std::sin(u * MathConstants<float>::twoPi * rb.freq * (0.85f + 0.16f * pitchNorm) + ph);
            const float mod = 0.72f * carrier + 0.28f * std::sin(u * MathConstants<float>::twoPi * (rb.freq * 1.75f) - ph * 1.25f);
            const float sibRipple = consonantLevel * 0.15f * std::sin(u * 34.0f + ph * 2.6f);
            const float yOff = win * envMod * baseAmp * rb.ampScale * (mod + sibRipple);

            const float x = leftX + u * width;
            const float y = cy + yOff;
            if (i == 0) p.startNewSubPath(x, y);
            else p.lineTo(x, y);
        }
        g.setColour(rb.col.withAlpha(0.10f + 0.09f * waveAmpSmooth));
        g.strokePath(p, PathStrokeType(rb.thickness * 6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(rb.col.withAlpha(0.26f + 0.16f * waveAmpSmooth));
        g.strokePath(p, PathStrokeType(rb.thickness * 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(rb.col.withAlpha(r == 0 ? 0.96f : 0.86f));
        g.strokePath(p, PathStrokeType(rb.thickness, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.restoreState();

    // Top-left inside glass lens: live pitch + note
    String pitchTxt = liveF0 > 0 ? (String(roundToInt(liveF0)) + " Hz") : String::fromUTF8("— Hz");
    if (liveF0 > 0) {
        static constexpr const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const int midi = roundToInt(69.0f + 12.0f * std::log2(liveF0 / 440.0f));
        pitchTxt += String::fromUTF8("  ·  ") + String(names[(midi % 12 + 12) % 12]) + String(midi / 12 - 1);
    }
    g.setColour(colours::ink);
    g.setFont(font(18.0f, 600));
    g.drawText(pitchTxt, Rectangle<float>(lens.getX() + 24.0f, lens.getY() + 12.0f, 220.0f, 24.0f), Justification::centredLeft);

    // Top-right inside glass lens: non-linear F1/F2/F3 formants & live hybrid state LED
    float fmtShift = proc.state.getRawParameterValue(ids::formant)->load();
    if (proc.state.getRawParameterValue(ids::follow)->load() > 0.5f)
        fmtShift += 0.25f * proc.state.getRawParameterValue(ids::pitch)->load();
    String fmtTxt;
    const String sep = String::fromUTF8("  ·  ");
    if (summary.formants[0] > 0) {
        fmtTxt = "F1 " + String(roundToInt(adub::VocalTractWarper::warpFormantFreq(summary.formants[0], fmtShift))) + sep + "F2 " +
                 String(roundToInt(adub::VocalTractWarper::warpFormantFreq(summary.formants[1], fmtShift))) + sep + "F3 " +
                 String(roundToInt(adub::VocalTractWarper::warpFormantFreq(summary.formants[2], fmtShift))) + " Hz";
    } else {
        fmtTxt = T("Formant analizi hazır", "Formant analyzer ready");
    }
    g.setColour(colours::dim);
    g.setFont(font(11.5f, 500));
    g.drawText(fmtTxt, Rectangle<float>(lens.getRight() - 480.0f, lens.getY() + 14.0f, 300.0f, 20.0f), Justification::centredRight);

    const float atVal = proc.state.getRawParameterValue(ids::autotune)->load();
    const bool activeState = atVal > 0.01f || voicedLevel > 0.35f || consonantLevel > 0.35f;
    const Colour stateCol = consonantLevel > 0.35f && voicedLevel <= 0.35f ? colours::cyan : colours::amber;
    const String stateChip = atVal > 0.01f ? ("Auto-Tune: " + keyScaleLabel(detectedKey, detectedScale))
                                           : (voicedLevel > 0.35f ? T("LF Sentez", "LF Synth")
                                                                  : (consonantLevel > 0.35f ? T("Ünsüz Koruma", "Guard") : T("Hazır", "Ready")));
    art::jewelLed(g, {lens.getRight() - 160.0f, lens.getY() + 24.0f}, 4.0f, activeState, stateCol);
    g.setColour(activeState ? colours::gold : colours::dim);
    g.setFont(font(11.5f, 600));
    g.drawText(stateChip, Rectangle<float>(lens.getRight() - 150.0f, lens.getY() + 14.0f, 134.0f, 20.0f), Justification::centredLeft);

    // Compact dual Liquid Glass Input & Output meters + centered Lookahead True-Peak Limiter GR pill
    const float my = lens.getBottom() - 22.0f;
    caption(g, T("Giriş", "In"), Rectangle<float>(lens.getX() + 24.0f, my - 2.0f, 40.0f, 14.0f), Justification::centredLeft, 9.5f);
    art::meter(g, {lens.getX() + 68.0f, my, 384.0f, 10.0f}, meterPosition(inLevel), meterPosition(inPeak));

    const bool limiting = limiterGrDb < -0.15f;
    auto grPill = Rectangle<float>(lens.getCentreX() - 72.0f, my - 4.0f, 144.0f, 18.0f);
    g.setColour(colours::well.withAlpha(0.85f));
    g.fillRoundedRectangle(grPill, 5.0f);
    g.setColour(limiting ? colours::amber.withAlpha(0.45f) : Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(grPill, 5.0f, 0.8f);
    g.setColour(limiting ? colours::amber : colours::faint);
    g.setFont(font(10.0f, 600));
    const String grTxt = limiting ? ("LIM " + String(limiterGrDb, 1) + " dB") : "TP -0.3 dBFS";
    g.drawText(grTxt, grPill, Justification::centred);

    caption(g, T("Çıkış", "Out"), Rectangle<float>(lens.getRight() - 452.0f, my - 2.0f, 44.0f, 14.0f), Justification::centredLeft, 9.5f);
    art::meter(g, {lens.getRight() - 404.0f, my, 380.0f, 10.0f}, meterPosition(outLevel), meterPosition(outPeak));
}

void AlptugDUBEditor::drawStage(Graphics& g) {
    using namespace ui;

    // 1. Brushed dark anthracite hardware chassis + oiled walnut wood end cheeks
    g.setGradientFill(ColourGradient(Colour(0xff151922), 0, 0, colours::bg, 0, (float) H, false));
    g.fillAll();
    g.setColour(Colours::white.withAlpha(0.015f));
    for (int y = 0; y < H; y += 3) g.drawHorizontalLine(y, 32.0f, (float) W - 32.0f);

    art::cheek(g, wood, {0.0f, 0.0f, 32.0f, (float) H}, true);
    art::cheek(g, wood, {(float) W - 32.0f, 0.0f, 32.0f, (float) H}, false);

    // Four machined hex socket screws fastening the chassis between walnut cheeks
    art::screw(g, {43.0f, 18.0f}, 5.0f, 0.4f);
    art::screw(g, {(float) W - 43.0f, 18.0f}, 5.0f, 1.2f);
    art::screw(g, {43.0f, (float) H - 18.0f}, 5.0f, 2.1f);
    art::screw(g, {(float) W - 43.0f, (float) H - 18.0f}, 5.0f, 0.9f);

    // 2. Top Liquid Glass header bar
    art::glassPanel(g, headerArea, 14.0f, 0.72f);
    {
        const auto f = font(24.0f, 900);
        g.setFont(f);
        const String a = "Alptug";
        const float aw = GlyphArrangement::getStringWidth(f, a);
        g.setColour(colours::ink);
        g.drawText(a, Rectangle<float>(72.0f, 20.0f, aw + 4.0f, 38.0f), Justification::left);
        g.setColour(colours::red);
        g.drawText("DUB", Rectangle<float>(72.0f + aw, 20.0f, 70.0f, 38.0f), Justification::left);

        // Version & maker
        auto verPill = Rectangle<float>(72.0f + aw + 60.0f, 30.0f, 32.0f, 18.0f);
        g.setColour(colours::amber.withAlpha(0.16f));
        g.fillRoundedRectangle(verPill, 5.0f);
        g.setColour(colours::amber);
        g.setFont(font(10.0f, 700));
        g.drawText("3.0", verPill, Justification::centred);
        caption(g, "by " + authorName, Rectangle<float>(72.0f + aw + 102.0f, 32.0f, 160.0f, 14.0f), Justification::left, 9.5f, colours::faint);

        // Center Liquid Glass voice profile & Auto-Tune key capsule
        auto profPill = Rectangle<float>(416.0f, 23.0f, 334.0f, 34.0f);
        g.setColour(colours::well.withAlpha(0.85f));
        g.fillRoundedRectangle(profPill, 8.0f);
        g.setColour(Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(profPill, 8.0f, 1.0f);
        const String sep = String::fromUTF8("  ·  ");
        const String profTxt = summary.ready() ? (String::fromUTF8(typeName(summary.type)) + sep + String(roundToInt(summary.medianF0)) + " Hz" + sep +
                                                  keyScaleLabel(detectedKey, detectedScale))
                                               : T("Sesinizi dinliyorum... ", "Listening... ") + String(summary.seconds, 1) + "s";
        g.setColour(summary.ready() ? colours::gold : colours::dim);
        g.setFont(font(12.0f, 500));
        g.drawText(profTxt, profPill.reduced(10, 0), Justification::centred);
    }

    // 3. Curved Liquid Glass wave oscilloscope
    drawGlassWave(g, lensArea);

    // 4. Middle symmetrical 3-module Liquid Glass analog console deck
    art::glassPanel(g, leftModArea, 16.0f, 0.72f);
    caption(g, T("Hibrit & Auto-Tune", "Hybrid & Auto-Tune"),
            Rectangle<float>(leftModArea.getX() + 16.0f, leftModArea.getY() + 10.0f, leftModArea.getWidth() - 32.0f, 14.0f), Justification::centred,
            10.0f, colours::dim);

    art::glassPanel(g, centerModArea, 16.0f, 0.78f);
    caption(g, T("Ana Dönüşüm", "Voice Core"),
            Rectangle<float>(centerModArea.getX() + 16.0f, centerModArea.getY() + 10.0f, centerModArea.getWidth() - 32.0f, 14.0f), Justification::centred,
            10.5f, colours::amber.withAlpha(0.85f));

    art::glassPanel(g, rightModArea, 16.0f, 0.72f);
    caption(g, T("Doku & Miks", "Tone & Mix"),
            Rectangle<float>(rightModArea.getX() + 16.0f, rightModArea.getY() + 10.0f, rightModArea.getWidth() - 32.0f, 14.0f), Justification::centred,
            10.0f, colours::dim);

    // 5. Bottom tactile analog character console bank
    art::glassPanel(g, charsArea, 16.0f, 0.74f);
}

void AlptugDUBEditor::timerCallback() {
    const float in = proc.inputLevel.load();
    const float out = proc.outputLevel.load();
    inLevel = in > inLevel ? in : inLevel * 0.85f;
    outLevel = out > outLevel ? out : outLevel * 0.85f;
    inPeak = jmax(inPeak - 0.006f, inLevel);
    outPeak = jmax(outPeak - 0.006f, outLevel);

    liveF0 = proc.liveF0.load();
    voicedLevel += (proc.liveVoiced.load() - voicedLevel) * 0.35f;
    consonantLevel += (proc.liveConsonant.load() - consonantLevel) * 0.35f;
    regenLevel += (proc.liveRegen.load() - regenLevel) * 0.35f;
    const float grNow = proc.liveLimiterGrDb.load();
    limiterGrDb = grNow < limiterGrDb ? grNow : limiterGrDb * 0.82f;
    detectedKey = proc.liveKey.load();
    detectedScale = proc.liveScale.load();
    tuneCents = proc.liveTuneCents.load();
    summary = proc.voiceSummary();

    const int wp = proc.waveWritePos.load(std::memory_order_acquire);
    for (int i = 0; i < 64; ++i)
        waveRing[(size_t) i] = proc.waveHistory[(size_t) ((wp + i) & 63)].load(std::memory_order_relaxed);
    const float targetWaveAmp = jlimit(0.0f, 1.0f, jmax(out * 1.15f, in * 0.85f));
    waveAmpSmooth += (targetWaveAmp - waveAmpSmooth) * (targetWaveAmp > waveAmpSmooth ? 0.38f : 0.14f);
    wavePhase += 0.12f + 0.22f * waveAmpSmooth + (liveF0 > 50.0f ? 0.04f * (liveF0 / 200.0f) : 0.0f);
    if (wavePhase > 10000.0f) wavePhase -= 10000.0f;

    const int ch = currentCharacter();
    for (int i = 0; i < characterButtons.size(); ++i) characterButtons[i]->setToggleState(i == ch, dontSendNotification);

    content.repaint();
}
