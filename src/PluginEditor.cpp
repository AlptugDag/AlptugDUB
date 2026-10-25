// AlptugDUB by Alptuğ Dağ — the editor.
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
        default: return "—";
    }
}

/** VU ballistics: the needle's position for an RMS level, -20 .. +3 VU (0 VU = -18 dBFS). */
float vuPosition(float rms) {
    const float db = Decibels::gainToDecibels(rms, -80.0f) + 18.0f;
    const float lo = std::pow(10.0f, -20.0f / 20.0f), hi = std::pow(10.0f, 3.0f / 20.0f);
    return jlimit(0.0f, 1.05f, (std::pow(10.0f, jlimit(-30.0f, 4.0f, db) / 20.0f) - lo) / (hi - lo));
}
} // namespace

// ---- Knob -------------------------------------------------------------------------------------------

Knob::Knob(AudioProcessorValueTreeState& s, const char* id, const String& name, const String& tip) : title(name) {
    slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(Slider::TextBoxBelow, false, 80, 18);
    slider.setColour(Slider::textBoxTextColourId, ui::colours::cream);
    slider.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setTooltip(tip);
    slider.setRotaryParameters(MathConstants<float>::pi * 1.22f, MathConstants<float>::pi * 2.78f, true);
    addAndMakeVisible(slider);
    attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, id, slider);
    slider.setDoubleClickReturnValue(true, (double) s.getParameter(id)->convertFrom0to1(s.getParameter(id)->getDefaultValue()));
}

void Knob::resized() { slider.setBounds(getLocalBounds().withTrimmedTop(18)); }

void Knob::paint(Graphics& g) {
    g.setColour(ui::colours::dim);
    g.setFont(ui::font(12.0f, 700));
    g.drawText(title.toUpperCase(), getLocalBounds().withHeight(16), Justification::centred);
}

// ---- About ------------------------------------------------------------------------------------------

AboutPage::AboutPage() {
    photo = ImageCache::getFromMemory(BinaryData::creator_jpg, BinaryData::creator_jpgSize);
    signature = ImageCache::getFromMemory(BinaryData::signature_png, BinaryData::signature_pngSize);
    const char* items[][2] = {{"Instagram", "https://instagram.com/alptugdag"},
                              {"YouTube", "https://youtube.com/@AlptugDag?sub_confirmation=1"},
                              {"Letterboxd", "https://letterboxd.com/AlptugDag/"},
                              {"LinkedIn", "https://www.linkedin.com/in/AlptugDag/"},
                              {"GitHub", "https://github.com/AlptugDag"},
                              {"CineTV", "https://github.com/AlptugDag/cinetv-launcher"}};
    for (auto& it : items) {
        auto* b = links.add(new TextButton(it[0]));
        const String url = it[1];
        b->onClick = [url] { URL(url).launchInDefaultBrowser(); };
        addAndMakeVisible(b);
    }
    close.setButtonText(T("Kapat", "Close"));
    close.onClick = [this] { setVisible(false); };
    addAndMakeVisible(close);
}

void AboutPage::resized() {
    auto card = getLocalBounds().withSizeKeepingCentre(720, 420);
    auto right = card.withTrimmedLeft(300).reduced(30, 0);
    int y = card.getY() + 250;
    for (int i = 0; i < links.size(); ++i) links[i]->setBounds(right.getX() + (i % 3) * 124, y + (i / 3) * 46, 116, 38);
    close.setBounds(right.getX(), card.getBottom() - 56, 116, 36);
}

void AboutPage::paint(Graphics& g) {
    g.fillAll(Colours::black.withAlpha(0.72f));
    auto card = getLocalBounds().withSizeKeepingCentre(720, 420).toFloat();
    ui::art::plate(g, card);
    auto pic = Rectangle<float>(card.getX() + 40, card.getY() + 50, 230, 288);
    g.saveState();
    Path round;
    round.addRoundedRectangle(pic, 14);
    g.reduceClipRegion(round);
    g.drawImage(photo, pic, RectanglePlacement::centred | RectanglePlacement::fillDestination);
    g.restoreState();
    g.setColour(Colours::white.withAlpha(0.18f));
    g.drawRoundedRectangle(pic, 14, 1);
    g.drawImage(signature, Rectangle<float>(card.getX() + 105, card.getY() + 340, 100, 64), RectanglePlacement::centred);

    auto text = card.withTrimmedLeft(300).reduced(30, 46);
    g.setColour(ui::colours::ink);
    g.setFont(ui::font(32, 900));
    g.drawText(String::fromUTF8("Alptuğ Dağ"), text.removeFromTop(42), Justification::left);
    g.setColour(ui::colours::amber);
    g.setFont(ui::font(12, 700));
    g.drawText(T("SİNEMACI · HİKÂYE ANLATICI · SESLENDİRME SANATÇISI", "FILMMAKER · STORYTELLER · VOICE ARTIST"), text.removeFromTop(22),
               Justification::left);
    g.setColour(ui::colours::dim);
    g.setFont(ui::font(14, 500));
    g.drawFittedText(T("2000'de Ankara'da doğdu. Yönettiği kısa film Sessiz (2023) ile tanınır. AlptugDUB'ı kendi "
                       "seslendirme ve dublaj işleri için tasarlayıp geliştirdi.",
                       "Born in Ankara in 2000, known for his short film Sessiz (2023). He designed and built AlptugDUB for "
                       "his own voice-over and dubbing work."),
                     text.removeFromTop(110).toNearestInt(), Justification::topLeft, 5);
}

// ---- Editor -------------------------------------------------------------------------------------------

AlptugDUBEditor::AlptugDUBEditor(AlptugDUBProcessor& p)
    : AudioProcessorEditor(p), proc(p),
      pitch(p.state, ids::pitch, T("Perde", "Pitch"), T("Sesin perdesi, yarım ton", "Voice pitch, semitones")),
      formant(p.state, ids::formant, T("Gırtlak", "Formant"),
              T("Ses yolunun boyu: aşağısı iri bir beden, yukarısı küçük", "Vocal tract size: down is a bigger body, up a smaller one")),
      range(p.state, ids::range, T("Tonlama", "Intonation"), T("Melodinin genişliği", "How wide the melody moves")),
      growl(p.state, ids::growl, T("Hırıltı", "Growl"), T("Ses tellerinin hırıltısı (vocal fry)", "Vocal-fold growl (vocal fry)")),
      age(p.state, ids::age, T("Yaş", "Age"), T("Yaşlı sesin titrekliği", "The tremble of an older voice")),
      breath(p.state, ids::breath, T("Nefes", "Breath"), T("Nefesli, fısıltılı ses", "Breathy, airy voice")),
      nasal(p.state, ids::nasal, T("Genizden", "Nasal"), T("Genizden konuşma", "Nasal resonance")),
      body(p.state, ids::body, T("Gövde", "Body"), T("Alt frekansların sıcaklığı", "Low-end warmth")),
      air(p.state, ids::air, T("Hava", "Air"), T("Üst frekansların parlaklığı", "Top-end air")),
      mix(p.state, ids::mix, T("Karışım", "Mix"), T("İşlenmiş ve orijinal ses", "Processed against original")),
      output(p.state, ids::output, T("Çıkış", "Output"), T("Çıkış seviyesi", "Output level")) {
    setLookAndFeel(&look);
    wood = ui::art::wood(W, H);
    signature = ImageCache::getFromMemory(BinaryData::signature_png, BinaryData::signature_pngSize);
    addAndMakeVisible(content);
    content.setSize(W, H);
    content.draw = [this](Graphics& g) { drawStage(g); };

    for (auto* k : {&pitch, &formant, &range, &growl, &age, &breath, &nasal, &body, &air, &mix, &output}) content.addAndMakeVisible(k);

    follow.setButtonText(T("Gırtlak perdeyi izlesin", "Formant follows pitch"));
    loudness.setButtonText(T("Ses seviyesini koru", "Keep loudness"));
    followAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state, ids::follow, follow);
    loudnessAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.state, ids::loudness, loudness);
    content.addAndMakeVisible(follow);
    content.addAndMakeVisible(loudness);

    const auto& chars = AlptugDUBProcessor::characters();
    for (int i = 0; i < (int) chars.size(); ++i) {
        auto* b = characterButtons.add(new TextButton(String::fromUTF8(turkish() ? chars[(size_t) i].nameTr : chars[(size_t) i].nameEn)));
        b->setClickingTogglesState(false);
        b->onClick = [this, i] { proc.applyCharacter(i); };
        content.addAndMakeVisible(b);
    }
    natural.setButtonText(T("Doğal ses", "Natural voice"));
    wide.setButtonText(T("Geniş", "Wide"));
    natural.setTooltip(T("Tek ses için en doğal motor (PSOLA)", "The most natural engine for one voice (PSOLA)"));
    wide.setTooltip(T("Müzik altında, koro ya da gürültülü kayıtlar için", "For voices over music, choirs or noisy takes"));
    natural.onClick = [this] { proc.state.getParameter(ids::engine)->setValueNotifyingHost(0.0f); };
    wide.onClick = [this] { proc.state.getParameter(ids::engine)->setValueNotifyingHost(1.0f); };
    relearn.setButtonText(T("Sesimi yeniden öğren", "Learn my voice again"));
    relearn.onClick = [this] { proc.resetVoiceProfile(); };
    aboutButton.setButtonText(T("Hakkında", "About"));
    aboutButton.onClick = [this] {
        about.setVisible(true);
        about.toFront(false);
    };
    for (auto* b : {&natural, &wide, &relearn, &aboutButton}) content.addAndMakeVisible(b);
    content.addChildComponent(about);

    layoutContent();
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double) W / H);
    setResizeLimits(W * 3 / 5, H * 3 / 5, W * 2, H * 2);
    setSize(W, H);
    startTimerHz(30);
}

AlptugDUBEditor::~AlptugDUBEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void AlptugDUBEditor::resized() {
    content.setTransform(AffineTransform::scale((float) getWidth() / W, (float) getHeight() / H));
}

void AlptugDUBEditor::layoutContent() {
    micArea = {64, 110, 220, 300};
    profileArea = {44, 420, 262, 222};
    relearn.setBounds(60, 598, 230, 30);

    pitch.setBounds(326, 112, 196, 214);
    formant.setBounds(528, 112, 196, 214);
    follow.setBounds(400, 334, 280, 24);
    Knob* small[] = {&range, &growl, &age, &breath, &nasal};
    for (int i = 0; i < 5; ++i) small[i]->setBounds(322 + i * 81, 370, 81, 118);
    vuInArea = {332, 508, 186, 122};
    vuOutArea = {530, 508, 186, 122};

    for (int i = 0; i < characterButtons.size(); ++i)
        characterButtons[i]->setBounds(748 + (i % 2) * 156, 136 + (i / 2) * 44, 150, 38);
    Knob* tone[] = {&body, &air, &mix, &output};
    for (int i = 0; i < 4; ++i) tone[i]->setBounds(744 + i * 78, 452, 78, 104);
    natural.setBounds(748, 566, 148, 32);
    wide.setBounds(904, 566, 146, 32);
    loudness.setBounds(752, 608, 296, 24);

    signatureArea = {870, 18, 120, 64};
    aboutButton.setBounds(996, 34, 84, 30);
    about.setBounds(0, 0, W, H);
}

void AlptugDUBEditor::drawStage(Graphics& g) {
    using namespace ui;
    g.drawImageAt(wood, 0, 0);

    // header on the wood: the name, the tagline, the signature
    g.setFont(font(38, 900));
    const String a = "Alptug", d = "DUB";
    const float aw = GlyphArrangement::getStringWidth(font(38, 900), a);
    g.setColour(Colours::black.withAlpha(0.45f));
    g.drawText(a, Rectangle<float>(34, 22, 300, 50), Justification::left);
    g.setColour(colours::cream);
    g.drawText(a, Rectangle<float>(32, 20, 300, 50), Justification::left);
    g.setColour(colours::red);
    g.drawText(d, Rectangle<float>(34 + aw, 20, 200, 50), Justification::left);
    g.setColour(colours::cream.withAlpha(0.75f));
    g.setFont(font(13, 600));
    g.drawText(T("Doğallığı bozmadan her karakter · by Alptuğ Dağ", "Every character, never unnatural · by Alptuğ Dağ"),
               Rectangle<float>(34, 64, 520, 20), Justification::left);
    g.setOpacity(0.92f);
    g.drawImage(signature, signatureArea, RectanglePlacement::centred);
    g.setOpacity(1.0f);

    art::plate(g, {24, 96, 1052, 560});

    // the microphone, lit by the voice
    art::microphone(g, micArea, micGlow);

    // voice profile
    auto pr = profileArea;
    g.setColour(Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(pr, 12);
    g.setColour(Colours::white.withAlpha(0.07f));
    g.drawRoundedRectangle(pr, 12, 1);
    auto inner = pr.reduced(16, 12);
    g.setColour(colours::dim);
    g.setFont(font(11.5f, 700));
    g.drawText(T("SES PROFİLİ", "VOICE PROFILE"), inner.removeFromTop(16), Justification::left);
    g.setColour(colours::ink);
    g.setFont(font(30, 900));
    const String now = liveF0 > 0 ? String(roundToInt(liveF0)) + " Hz" : String::fromUTF8("—");
    g.drawText(now, inner.removeFromTop(40), Justification::left);
    g.setFont(font(13, 600));
    if (summary.ready()) {
        g.setColour(colours::amber);
        g.drawText(String::fromUTF8(typeName(summary.type)) + "  ·  " + T("orta ", "middle ") + String(roundToInt(summary.medianF0)) + " Hz  (" +
                       String(roundToInt(summary.lowF0)) + "–" + String(roundToInt(summary.highF0)) + ")",
                   inner.removeFromTop(20), Justification::left);
        g.setColour(colours::dim);
        g.drawText("F1 " + String(roundToInt(summary.formants[0])) + "   F2 " + String(roundToInt(summary.formants[1])) + "   F3 " +
                       String(roundToInt(summary.formants[2])) + " Hz",
                   inner.removeFromTop(20), Justification::left);
        g.drawText(T("Parlaklık ", "Brightness ") + String(roundToInt(summary.brightness)) + " Hz   " + T("Nefes ", "Breath ") +
                       String(roundToInt(summary.breathiness * 100)) + "%",
                   inner.removeFromTop(20), Justification::left);
        g.setColour(colours::faint);
        g.setFont(font(11.5f, 500));
        g.drawFittedText(T("Karakterler bu sese göre ayarlanır.", "Characters are tuned to this voice."), inner.removeFromTop(34).toNearestInt(),
                         Justification::topLeft, 2);
    } else {
        g.setColour(colours::dim);
        g.drawFittedText(T("Birkaç saniye konuşun: AlptugDUB sesinizin karakterini öğrenir, karakterleri ona göre ayarlar.",
                           "Speak for a few seconds: AlptugDUB learns your voice and tunes every character to it."),
                         inner.removeFromTop(56).toNearestInt(), Justification::topLeft, 3);
        auto bar = inner.removeFromTop(8).withWidth(inner.getWidth());
        g.setColour(Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(bar, 4);
        g.setColour(colours::amber);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * jlimit(0.0f, 1.0f, summary.seconds / 2.0f)), 4);
    }

    // section titles
    g.setColour(colours::dim);
    g.setFont(font(11.5f, 700));
    g.drawText(T("KARAKTER", "CHARACTER"), Rectangle<float>(750, 112, 300, 16), Justification::left);
    g.setColour(Colours::white.withAlpha(0.06f));
    g.drawLine(316, 120, 316, 640, 1);
    g.drawLine(734, 120, 734, 640, 1);
    g.drawLine(744, 446, 1052, 446, 1);
    g.drawLine(326, 500, 722, 500, 1);

    art::vu(g, vuInArea, vuIn, T("GİRİŞ", "INPUT"));
    art::vu(g, vuOutArea, vuOut, T("ÇIKIŞ", "OUTPUT"));
}

void AlptugDUBEditor::timerCallback() {
    // VU ballistics: about 300 ms to settle, like the real thing
    const float k = 1.0f - std::exp(-1.0f / (0.3f * 30.0f));
    vuIn += (vuPosition(proc.inputLevel.load()) - vuIn) * k * 3.0f;
    vuOut += (vuPosition(proc.outputLevel.load()) - vuOut) * k * 3.0f;
    micGlow += (jlimit(0.0f, 1.0f, proc.inputLevel.load() * 6.0f) - micGlow) * 0.25f;
    liveF0 = proc.liveF0.load();
    summary = proc.voiceSummary();
    const int ch = roundToInt(proc.state.getRawParameterValue(ids::character)->load());
    for (int i = 0; i < characterButtons.size(); ++i) characterButtons[i]->setToggleState(i == ch, dontSendNotification);
    const bool w = proc.state.getRawParameterValue(ids::engine)->load() > 0.5f;
    natural.setToggleState(!w, dontSendNotification);
    wide.setToggleState(w, dontSendNotification);
    content.repaint(micArea.toNearestInt().expanded(10));
    content.repaint(profileArea.toNearestInt());
    content.repaint(vuInArea.toNearestInt());
    content.repaint(vuOutArea.toNearestInt());
}
