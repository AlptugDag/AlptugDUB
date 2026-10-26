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

/** Level meter scale: -60 .. 0 dBFS. */
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
    r.removeFromTop(large ? 30 : 18);
    r.removeFromBottom(large ? 32 : 18);
    slider.setBounds(r);
}

void Knob::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat();
    ui::caption(g, title, r.removeFromTop(large ? 26.0f : 15.0f), Justification::centred, large ? 15.0f : 10.5f);
    g.setColour(large ? ui::colours::ink : ui::colours::dim);
    g.setFont(ui::font(large ? 20.0f : 12.0f, large ? 600 : 500));
    g.drawText(slider.getTextFromValue(slider.getValue()), r.removeFromBottom(large ? 30.0f : 16.0f), Justification::centred);
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
    ui::art::panel(g, card, 20);
    auto pic = Rectangle<float>(card.getX() + 34, card.getY() + 34, 262, 330);
    ui::art::photo(g, photo, pic, 14);
    if (signature)
        signature->drawWithin(g, Rectangle<float>(pic.getX() + 50, pic.getBottom() + 8, pic.getWidth() - 100, 90), RectanglePlacement::centred, 0.95f);

    auto text = card.withTrimmedLeft(330).reduced(34, 40);
    g.setColour(ui::colours::ink);
    g.setFont(ui::font(36, 900));
    g.drawText(authorName, text.removeFromTop(48), Justification::left);
    ui::caption(g, T("Sinemacı · Hikâye anlatıcı · Seslendirme sanatçısı", "Filmmaker · Storyteller · Voice artist"), text.removeFromTop(24),
                Justification::left, 11.5f, ui::colours::cyan);
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
               Rectangle<float>(text.getX(), card.getBottom() - 52, 400, 20), Justification::left);
}

// ---- Editor -------------------------------------------------------------------------------------------

AlptugDUBEditor::AlptugDUBEditor(AlptugDUBProcessor& p)
    : AudioProcessorEditor(p), proc(p),
      pitch(p.state, ids::pitch, T("Perde", "Pitch"), T("Sesin perdesi, yarım ton. Çift tık: sıfırla", "Voice pitch in semitones. Double-click: reset"), true),
      formant(p.state, ids::formant, T("Gırtlak", "Formant"),
              T("Ses yolunun boyu: aşağısı iri bir beden, yukarısı küçük", "Vocal tract size: down is a bigger body, up a smaller one"), true),
      age(p.state, ids::age, T("Yaş", "Age"),
          T("Eksi: daha genç ve parlak. Artı: daha yaşlı ve koyu", "Minus: younger and brighter. Plus: older and darker")),
      mix(p.state, ids::mix, T("Karışım", "Mix"), T("İşlenmiş ve orijinal ses", "Processed against original")),
      output(p.state, ids::output, T("Çıkış", "Output"), T("Çıkış seviyesi", "Output level")) {
    ui::setTurkish(turkish());
    setLookAndFeel(&look);
    wood = ImageCache::getFromMemory(BinaryData::wood_jpg, BinaryData::wood_jpgSize);
    signature = signatureIn(ui::colours::ink);
    addAndMakeVisible(content);
    content.setSize(W, H);
    content.draw = [this](Graphics& g) { drawStage(g); };

    for (auto* k : {&pitch, &formant, &age, &mix, &output}) content.addAndMakeVisible(k);

    // the AI switches: each hands a part of the voice to the analysis
    const char* sw[][5] = {
        {ids::ai, "Yapay zekâ perde analizi", "AI pitch analysis",
         "Sinirsel perde modeli (SwiftF0) sesinizi öğrenir: perde aralığı, ses tipi, formantlar. Karakterler buna göre ayarlanır. Kapalıyken hızlı analiz kullanılır.",
         "The neural pitch model (SwiftF0) learns your voice: pitch range, voice type, formants. Characters are tuned to it. Off uses the fast tracker."},
        {ids::autoMatch, "Karakteri sesime uyarla", "Fit character to my voice",
         "Öğrenilen sesinize göre karakterin perdesini ve gırtlağını sürekli yeniden hesaplar.",
         "Keeps recomputing the character's pitch and formants from your learned voice."},
        {ids::autoTone, "Akıllı ton dengesi", "Smart tone",
         "Kalınlaşan sese göğüs, incelen sese hava ekler; ton hiç donuklaşmaz.", "Adds chest to a deeper voice and air to a lighter one."},
        {ids::loudness, "Seviyeyi koru", "Keep level", "İşlenmiş ses orijinal kadar yüksek kalır.", "The processed voice stays as loud as the original."},
        {ids::follow, "Gırtlak perdeyi izlesin", "Formant follows pitch", "Gırtlak perdeyle birlikte biraz kayar.",
         "Formants move a little with the pitch."},
    };
    for (auto& d : sw) {
        auto* b = switches.add(new ToggleButton(T(d[1], d[2])));
        b->setTooltip(T(d[3], d[4]));
        content.addAndMakeVisible(b);
        switchAttachments.add(new AudioProcessorValueTreeState::ButtonAttachment(p.state, d[0], *b));
    }

    const auto& chars = AlptugDUBProcessor::characters();
    for (int i = 0; i < (int) chars.size(); ++i) {
        auto* b = characterButtons.add(new TextButton(String::fromUTF8(turkish() ? chars[(size_t) i].nameTr : chars[(size_t) i].nameEn)));
        b->getProperties().set("segment", true);
        b->onClick = [this, i] { proc.applyCharacter(i); };
        content.addAndMakeVisible(b);
    }
    relearn.setButtonText(T("Sesimi yeniden öğren", "Learn my voice again"));
    relearn.onClick = [this] { proc.resetVoiceProfile(); };
    aboutButton.setButtonText(T("Hakkında", "About"));
    aboutButton.onClick = [this] {
        about.setVisible(true);
        about.toFront(false);
    };
    for (auto* b : {&relearn, &aboutButton}) content.addAndMakeVisible(b);
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

void AlptugDUBEditor::resized() {
    content.setTransform(AffineTransform::scale((float) getWidth() / W, (float) getHeight() / H));
}

int AlptugDUBEditor::currentCharacter() const { return roundToInt(proc.state.getRawParameterValue(ids::character)->load()); }

void AlptugDUBEditor::layoutContent() {
    // top bar
    aboutButton.setBounds(1084, 16, 80, 40);

    panelArea = {44, 84, 1112, 620};
    analysisArea = {68, 108, 272, 516};
    relearn.setBounds(68, 636, 272, 44);

    // the two that matter most, and the switches under them
    pitch.setBounds(372, 100, 210, 252);
    formant.setBounds(602, 100, 210, 252);
    switchesArea = {372, 380, 440, 172};
    for (int i = 0; i < switches.size(); ++i)
        switches[i]->setBounds((int) switchesArea.getX() + (i % 2) * 226, (int) switchesArea.getY() + 24 + (i / 2) * 37, 222, 32);

    // a few knobs, three across, then the meters
    Knob* grid[] = {&age, &mix, &output};
    for (int i = 0; i < 3; ++i) grid[i]->setBounds(842 + i * 97, 130, 97, 110);
    metersArea = {856, 290, 268, 236};

    // characters, lit like a display
    charactersArea = {364, 592, 768, 92};
    const int n = characterButtons.size(), perRow = (n + 1) / 2;
    for (int i = 0; i < n; ++i) {
        const int row = i / perRow, col = i % perRow, inRow = row == 0 ? perRow : n - perRow;
        const float w = (charactersArea.getWidth() - 8) / (float) inRow;
        characterButtons[i]->setBounds(Rectangle<float>(charactersArea.getX() + 4 + w * (float) col, charactersArea.getY() + 4 + 42 * (float) row, w, 42)
                                           .reduced(2, 2)
                                           .toNearestInt());
    }

    about.setBounds(0, 0, W, H);
}

void AlptugDUBEditor::drawStage(Graphics& g) {
    using namespace ui;
    // the console, between walnut cheeks
    g.setGradientFill(ColourGradient(Colour(0xff111528), 0, 0, colours::bg, 0, (float) H, false));
    g.fillAll();
    art::cheek(g, wood, {0, 0, 26, (float) H}, true);
    art::cheek(g, wood, {(float) W - 26, 0, 26, (float) H}, false);

    // name, maker, signature
    const auto f = font(30, 900);
    g.setFont(f);
    const String a = "Alptug";
    const float aw = GlyphArrangement::getStringWidth(f, a);
    g.setColour(colours::ink);
    g.drawText(a, Rectangle<float>(52, 16, 200, 42), Justification::left);
    g.setColour(colours::red);
    g.drawText("DUB", Rectangle<float>(52 + aw, 16, 120, 42), Justification::left);
    caption(g, "by " + authorName, Rectangle<float>(56 + aw + 80, 29, 200, 16), Justification::left, 11.0f, colours::faint);
    if (signature) signature->drawWithin(g, Rectangle<float>(990, 6, 84, 60), RectanglePlacement::centred, 0.9f);

    // the instrument panel, screwed down
    art::panel(g, panelArea, 18);
    for (auto c : {panelArea.getTopLeft(), panelArea.getTopRight(), panelArea.getBottomLeft(), panelArea.getBottomRight()}) {
        const float dx = c.x < W / 2 ? 14.0f : -14.0f, dy = c.y < H / 2 ? 14.0f : -14.0f;
        art::screw(g, c.translated(dx, dy), 5.5f, c.x * 0.013f + c.y * 0.007f);
    }
    drawAnalysis(g);

    // dividers
    g.setColour(Colours::white.withAlpha(0.04f));
    g.drawVerticalLine(354, panelArea.getY() + 24, panelArea.getBottom() - 24);
    g.drawVerticalLine(832, panelArea.getY() + 24, charactersArea.getY() - 36);
    g.drawHorizontalLine((int) switchesArea.getY() - 12, switchesArea.getX(), switchesArea.getRight());

    caption(g, T("Yapay zekâ", "AI"), Rectangle<float>(switchesArea.getX() + 2, switchesArea.getY(), 300, 14), Justification::left, 12.0f);

    // meters and how sure the network is that it hears a voice
    caption(g, T("Giriş", "Input"), Rectangle<float>(metersArea.getX(), metersArea.getY(), 200, 14), Justification::left, 10.5f);
    art::meter(g, {metersArea.getX(), metersArea.getY() + 20, metersArea.getWidth(), 12}, inLevel, inPeak);
    caption(g, T("Çıkış", "Output"), Rectangle<float>(metersArea.getX(), metersArea.getY() + 48, 200, 14), Justification::left, 10.5f);
    art::meter(g, {metersArea.getX(), metersArea.getY() + 68, metersArea.getWidth(), 12}, outLevel, outPeak);
    caption(g, T("Yapay zekâ güveni", "AI confidence"), Rectangle<float>(metersArea.getX(), metersArea.getY() + 96, 200, 14), Justification::left, 10.5f);
    {
        auto r = Rectangle<float>(metersArea.getX(), metersArea.getY() + 116, metersArea.getWidth(), 12);
        g.setColour(colours::well);
        g.fillRoundedRectangle(r, 6);
        const bool on = proc.state.getRawParameterValue(ids::ai)->load() > 0.5f;
        if (on && confidence > 0.01f) {
            Path bar;
            bar.addRoundedRectangle(r.reduced(2).withWidth((r.getWidth() - 4) * confidence), 4);
            art::glow(g, bar, 0.6f);
            g.setGradientFill(ColourGradient(colours::blue, r.getX(), 0, colours::cyan, r.getRight(), 0, false));
            g.fillPath(bar);
        }
        g.setColour(colours::dim);
        g.setFont(font(12, 500));
        g.drawText(on ? String(roundToInt(confidence * 100)) + "%" : T("kapalı", "off"), r.translated(0, -18).withTrimmedLeft(r.getWidth() - 60),
                   Justification::right);
    }
    g.setColour(colours::faint);
    g.setFont(font(11.5f, 500));
    g.drawFittedText(T("Düğmeye çift tık: varsayılan değer. Yapay zekânın yönettiği ince ayarlar DAW otomasyonunda da açık.",
                       "Double-click a knob for its default. The fine settings the AI manages are also open to your DAW's automation."),
                     Rectangle<float>(metersArea.getX(), metersArea.getY() + 150, metersArea.getWidth(), 50).toNearestInt(), Justification::topLeft, 3);

    // character selector
    caption(g, T("Karakter", "Character"), Rectangle<float>(charactersArea.getX() + 4, charactersArea.getY() - 28, 300, 16), Justification::left, 12.0f);
    g.setColour(colours::well);
    g.fillRoundedRectangle(charactersArea, 12);
    g.setColour(Colours::white.withAlpha(0.06f));
    g.drawRoundedRectangle(charactersArea.reduced(0.5f), 12, 1);
}

void AlptugDUBEditor::drawAnalysis(Graphics& g) {
    using namespace ui;
    // what AlptugDUB hears and has learned about this voice
    g.setColour(colours::well);
    g.fillRoundedRectangle(analysisArea, 14);
    g.setColour(Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(analysisArea.reduced(0.5f), 14, 1);
    auto inner = analysisArea.reduced(18, 16);
    auto head = inner.removeFromTop(22);
    caption(g, T("Ses analizi", "Voice analysis"), head, Justification::left, 12.0f);
    const bool ready = summary.ready();
    const String chip = ready ? T("Öğrenildi", "Learned") : T("Dinleniyor", "Listening");
    g.setFont(font(11.0f, 600));
    const float cw = GlyphArrangement::getStringWidth(font(11.0f, 600), chip) + 22;
    auto chipR = head.removeFromRight(cw).withSizeKeepingCentre(cw, 20);
    g.setColour(colours::cyan.withAlpha(ready ? 0.16f : 0.08f));
    g.fillRoundedRectangle(chipR, 10);
    g.setColour(ready ? colours::cyan : colours::dim);
    g.drawText(chip, chipR, Justification::centred);

    // the pitch right now, with its note name
    inner.removeFromTop(8);
    auto live = inner.removeFromTop(56);
    g.setColour(colours::ink);
    g.setFont(font(42, 600));
    const String hz = liveF0 > 0 ? String(roundToInt(liveF0)) : String::fromUTF8("—");
    g.drawText(hz, live.withWidth(140), Justification::bottomLeft);
    const float hw = GlyphArrangement::getStringWidth(font(42, 600), hz);
    String note;
    if (liveF0 > 0) {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const int midi = roundToInt(69 + 12 * std::log2(liveF0 / 440.0f));
        note = "  " + String(names[(midi % 12 + 12) % 12]) + String(midi / 12 - 1);
    }
    g.setColour(colours::dim);
    g.setFont(font(15, 500));
    g.drawText("Hz" + note, Rectangle<float>(live.getX() + hw + 8, live.getY(), 150, live.getHeight() - 8), Justification::bottomLeft);

    inner.removeFromTop(6);
    g.setFont(font(13, 500));
    if (ready) {
        g.setColour(colours::cyan);
        g.drawText(String::fromUTF8(typeName(summary.type)) + "  ·  " + T("orta ", "middle ") + String(roundToInt(summary.medianF0)) + " Hz  (" +
                       String(roundToInt(summary.lowF0)) + String::fromUTF8("–") + String(roundToInt(summary.highF0)) + ")",
                   inner.removeFromTop(20), Justification::left);
        g.setColour(colours::dim);
        g.drawText(T("Parlaklık ", "Brightness ") + String(roundToInt(summary.brightness)) + " Hz", inner.removeFromTop(20), Justification::left);
    } else {
        g.setColour(colours::dim);
        g.drawFittedText(T("Birkaç saniye konuşun: AlptugDUB sesinizi öğrenir ve karakterleri ona göre ayarlar.",
                           "Speak for a few seconds: AlptugDUB learns your voice and tunes every character to it."),
                         inner.removeFromTop(40).toNearestInt(), Justification::topLeft, 2);
        auto bar = inner.removeFromTop(10).withSizeKeepingCentre(inner.getWidth(), 4);
        g.setColour(Colours::white.withAlpha(0.06f));
        g.fillRoundedRectangle(bar, 2);
        g.setColour(colours::cyan);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * jlimit(0.0f, 1.0f, summary.seconds / 2.0f)), 2);
    }

    // the vocal tract: formants on a log axis, 200 Hz .. 4 kHz, and where the Formant knob moves them
    inner.removeFromTop(14);
    caption(g, T("Formantlar", "Formants"), inner.removeFromTop(14), Justification::left, 10.0f, colours::faint);
    inner.removeFromTop(4);
    auto tractBox = inner.removeFromTop(64);
    auto xOf = [&](float f) {
        const float t = std::log(jlimit(200.0f, 4000.0f, f) / 200.0f) / std::log(20.0f);
        return tractBox.getX() + 8 + t * (tractBox.getWidth() - 16);
    };
    g.setFont(font(10.0f, 500));
    for (float f : {500.0f, 1000.0f, 2000.0f}) {
        g.setColour(Colours::white.withAlpha(0.05f));
        g.drawVerticalLine((int) xOf(f), tractBox.getY() + 4, tractBox.getBottom() - 16);
        g.setColour(colours::faint);
        g.drawText(f >= 1000 ? String(f / 1000, 0) + "k" : String((int) f), Rectangle<float>(xOf(f) - 20, tractBox.getBottom() - 14, 40, 12),
                   Justification::centred);
    }
    const float shift = std::pow(2.0f, proc.state.getRawParameterValue(ids::formant)->load() / 12.0f);
    for (int k = 0; k < 3; ++k) {
        const float f = summary.formants[(size_t) k];
        if (f <= 0) continue;
        const float x0 = xOf(f), x1 = xOf(f * shift);
        g.setColour(colours::dim.withAlpha(0.35f));
        g.drawLine(x0, tractBox.getY() + 14, x0, tractBox.getBottom() - 18, 2.0f);
        Path bar;
        bar.addRoundedRectangle(x1 - 2, tractBox.getY() + 12, 4, tractBox.getHeight() - 30, 2);
        art::glow(g, bar, 0.6f);
        g.setColour(colours::cyan);
        g.fillPath(bar);
        g.setColour(colours::ink);
        g.drawText("F" + String(k + 1), Rectangle<float>(x1 - 15, tractBox.getY(), 30, 10), Justification::centred);
    }

    // the pitch of the last six seconds, on a log scale from 60 to 500 Hz
    inner.removeFromTop(14);
    caption(g, T("Perde, son 6 saniye", "Pitch, last 6 seconds"), inner.removeFromTop(14), Justification::left, 10.0f, colours::faint);
    inner.removeFromTop(6);
    auto graph = inner;
    auto yOf = [&](float f) {
        const float t = std::log(jlimit(60.0f, 500.0f, f) / 60.0f) / std::log(500.0f / 60.0f);
        return graph.getBottom() - 8 - t * (graph.getHeight() - 16);
    };
    if (ready) {
        g.setColour(colours::blue.withAlpha(0.08f));
        g.fillRect(Rectangle<float>(graph.getX(), yOf(summary.highF0), graph.getWidth(), yOf(summary.lowF0) - yOf(summary.highF0)));
    }
    g.setFont(font(10.0f, 500));
    for (float f : {100.0f, 200.0f, 400.0f}) {
        g.setColour(Colours::white.withAlpha(0.05f));
        g.drawHorizontalLine((int) yOf(f), graph.getX(), graph.getRight());
        g.setColour(colours::faint);
        g.drawText(String((int) f), Rectangle<float>(graph.getX() + 2, yOf(f) - 13, 40, 12), Justification::left);
    }
    Path trace;
    bool pen = false;
    const int n = (int) history.size();
    for (int i = 0; i < n; ++i) {
        const float f = history[(size_t) ((historyPos + i) % n)];
        const float x = graph.getX() + 4 + (graph.getWidth() - 8) * (float) i / (float) (n - 1);
        if (f <= 0) {
            pen = false;
            continue;
        }
        if (pen) trace.lineTo(x, yOf(f));
        else trace.startNewSubPath(x, yOf(f)), trace.lineTo(x + 0.5f, yOf(f));
        pen = true;
    }
    art::glow(g, trace, 0.8f);
    g.setGradientFill(ColourGradient(colours::blue, graph.getX(), 0, colours::cyan, graph.getRight(), 0, false));
    g.strokePath(trace, PathStrokeType(2.2f, PathStrokeType::curved, PathStrokeType::rounded));
}

void AlptugDUBEditor::timerCallback() {
    const float in = meterPosition(proc.inputLevel.load()), out = meterPosition(proc.outputLevel.load());
    inLevel = in > inLevel ? in : inLevel * 0.86f;
    outLevel = out > outLevel ? out : outLevel * 0.86f;
    inPeak = jmax(inPeak - 0.006f, in);
    outPeak = jmax(outPeak - 0.006f, out);
    liveF0 = proc.liveF0.load();
    history[(size_t) historyPos] = liveF0;
    historyPos = (historyPos + 1) % (int) history.size();
    summary = proc.voiceSummary();

    const int ch = currentCharacter();
    for (int i = 0; i < characterButtons.size(); ++i) characterButtons[i]->setToggleState(i == ch, dontSendNotification);
    content.repaint(analysisArea.toNearestInt().expanded(2));
    content.repaint(metersArea.toNearestInt().expanded(2));
    confidence += (proc.liveConfidence.load() - confidence) * 0.3f;
}
