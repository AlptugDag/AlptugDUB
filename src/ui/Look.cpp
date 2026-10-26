// AlptugDUB by Alptuğ Dağ — the look.
#include "Look.h"

#include <BinaryData.h>

namespace ui {

using namespace juce;

Font font(float height, int weight) {
    static Typeface::Ptr medium = Typeface::createSystemTypefaceFor(BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
    static Typeface::Ptr bold = Typeface::createSystemTypefaceFor(BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
    static Typeface::Ptr black = Typeface::createSystemTypefaceFor(BinaryData::PoppinsBlack_ttf, BinaryData::PoppinsBlack_ttfSize);
    const auto& face = weight >= 900 ? black : weight >= 650 ? bold : medium;
    return Font(FontOptions(face).withHeight(height));
}

static bool turkishCase = false;
void setTurkish(bool tr) { turkishCase = tr; }

String upper(const String& text) {
    // JUCE's upper-casing leaves ı ş ğ ü ö ç alone, and Turkish capitalises i as İ
    String out;
    for (auto ch : text) {
        switch (ch) {
            case 0x131: ch = 'I'; break;   // ı
            case 0x15F: ch = 0x15E; break; // ş
            case 0x11F: ch = 0x11E; break; // ğ
            case 0xFC: ch = 0xDC; break;   // ü
            case 0xF6: ch = 0xD6; break;   // ö
            case 0xE7: ch = 0xC7; break;   // ç
            case 0xE2: ch = 0xC2; break;   // â
            case 'i': ch = turkishCase ? (juce_wchar) 0x130 : (juce_wchar) 'I'; break;
            default: ch = CharacterFunctions::toUpperCase(ch);
        }
        out += ch;
    }
    return out;
}

void caption(Graphics& g, const String& text, Rectangle<float> r, Justification j, float height, Colour c) {
    g.setColour(c);
    g.setFont(font(height, 500).withExtraKerningFactor(0.24f));
    g.drawText(upper(text), r, j, false);
}

Look::Look() {
    setColour(TooltipWindow::backgroundColourId, colours::panel);
    setColour(TooltipWindow::textColourId, colours::ink);
    setColour(TooltipWindow::outlineColourId, Colours::white.withAlpha(0.08f));
    setColour(PopupMenu::backgroundColourId, colours::panel);
    setColour(PopupMenu::textColourId, colours::ink);
    setColour(PopupMenu::highlightedBackgroundColourId, colours::blue);
    setColour(PopupMenu::highlightedTextColourId, Colours::white);
    setColour(BubbleComponent::backgroundColourId, colours::panel);
    setColour(BubbleComponent::outlineColourId, Colours::white.withAlpha(0.1f));
}

void Look::drawBubble(Graphics& g, BubbleComponent&, const juce::Point<float>&, const Rectangle<float>& body) {
    g.setColour(colours::panel);
    g.fillRoundedRectangle(body, 8);
    g.setColour(colours::blue.withAlpha(0.6f));
    g.drawRoundedRectangle(body.reduced(0.5f), 8, 1);
}

// ---- knobs ------------------------------------------------------------------------------------------

namespace {
juce::Point<float> polar(juce::Point<float> c, float r, float a) { return {c.x + std::sin(a) * r, c.y - std::cos(a) * r}; }
Colour blueAt(float t) { return colours::blue.interpolatedWith(colours::cyan, jlimit(0.0f, 1.0f, t)); }
} // namespace

void Look::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s) {
    const auto box = Rectangle<float>((float) x, (float) y, (float) w, (float) h);
    const auto c = box.getCentre();
    const float R = jmin(box.getWidth(), box.getHeight()) * 0.5f - 2.0f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? start + (end - start) * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : start;
    const float lo = jmin(from, angle), hi = jmax(from, angle);

    // the dotted scale: lit from the rest position to the value
    const int dots = R > 60 ? 36 : 24;
    const float dotR = jmax(1.5f, R * 0.032f);
    for (int i = 0; i < dots; ++i) {
        const float a = start + (end - start) * (float) i / (float) (dots - 1);
        const bool lit = a >= lo - 1e-3f && a <= hi + 1e-3f;
        const auto p = polar(c, R - dotR, a);
        if (lit) {
            const Colour col = blueAt((a - start) / (end - start));
            g.setColour(col.withAlpha(0.25f));
            g.fillEllipse(Rectangle<float>(dotR * 4.0f, dotR * 4.0f).withCentre(p));
            g.setColour(col);
        } else {
            g.setColour(Colour(0xff05070f));
        }
        g.fillEllipse(Rectangle<float>(dotR * 2.0f, dotR * 2.0f).withCentre(p));
    }

    // the knob: a soft dome casting a shadow
    const float kr = R * 0.76f;
    Path body;
    body.addEllipse(c.x - kr, c.y - kr, kr * 2, kr * 2);
    DropShadow(Colours::black.withAlpha(0.75f), (int) (kr * 0.45f), {0, (int) (kr * 0.18f)}).drawForPath(g, body);
    g.setGradientFill(ColourGradient(Colour(0xff2f3560), c.x - kr * 0.6f, c.y - kr * 0.8f, Colour(0xff151935), c.x + kr * 0.5f, c.y + kr, false));
    g.fillPath(body);
    g.setColour(Colours::white.withAlpha(0.07f));
    g.drawEllipse(c.x - kr + 0.5f, c.y - kr + 0.5f, kr * 2 - 1, kr * 2 - 1, 1.0f);

    // the lit edge of the dome, following the value
    if (hi - lo > 0.01f) {
        Path arc;
        arc.addCentredArc(c.x, c.y, kr - 1.5f, kr - 1.5f, 0, lo, hi, true);
        const float stroke = jmax(2.0f, kr * 0.055f);
        art::glow(g, arc, 0.9f);
        g.setGradientFill(ColourGradient(colours::blue, polar(c, kr, lo), colours::cyan, polar(c, kr, hi), false));
        g.strokePath(arc, PathStrokeType(stroke, PathStrokeType::curved, PathStrokeType::rounded));
    }
    // the pointer
    const auto p = polar(c, kr * 0.78f, angle);
    const float pr = jmax(2.5f, kr * 0.07f);
    g.setColour(colours::cyan.withAlpha(s.isMouseOverOrDragging() ? 0.45f : 0.25f));
    g.fillEllipse(Rectangle<float>(pr * 4, pr * 4).withCentre(p));
    g.setColour(colours::cyan);
    g.fillEllipse(Rectangle<float>(pr * 2, pr * 2).withCentre(p));
}

// ---- switches and buttons ---------------------------------------------------------------------------

void Look::drawToggleButton(Graphics& g, ToggleButton& b, bool highlighted, bool) {
    auto r = b.getLocalBounds().toFloat();
    const float h = jmin(r.getHeight() - 4, 22.0f);
    auto track = Rectangle<float>(r.getX() + 2, r.getCentreY() - h / 2, h * 1.85f, h);
    const bool on = b.getToggleState();
    if (on) {
        Path p;
        p.addRoundedRectangle(track, h / 2);
        art::glow(g, p, 0.7f);
        g.setGradientFill(ColourGradient(colours::cyan, track.getX(), 0, colours::blue, track.getRight(), 0, false));
        g.fillPath(p);
    } else {
        g.setColour(colours::well);
        g.fillRoundedRectangle(track, h / 2);
        g.setColour(Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(track.reduced(0.5f), h / 2, 1.0f);
    }
    const float kx = on ? track.getRight() - h + 3 : track.getX() + 3;
    g.setColour(on ? Colours::white : colours::dim);
    g.fillEllipse(kx, track.getY() + 3, h - 6, h - 6);
    g.setColour(highlighted || on ? colours::ink : colours::dim);
    g.setFont(font(13.5f, 500));
    g.drawText(b.getButtonText(), Rectangle<float>(track.getRight() + 10, r.getY(), r.getRight() - track.getRight() - 10, r.getHeight()),
               Justification::centredLeft);
}

void Look::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool highlighted, bool down) {
    auto r = b.getLocalBounds().toFloat();
    const bool segment = b.getProperties()["segment"];
    if (b.getToggleState()) {
        // the selected segment: lit from behind, with fine vertical lines like a display
        Path p;
        p.addRoundedRectangle(r.reduced(1.0f), segment ? 6.0f : 10.0f);
        art::glow(g, p, 1.0f);
        g.setGradientFill(ColourGradient(Colour(0xff28c4ff), r.getX(), r.getY(), Colour(0xff0a8dff), r.getX(), r.getBottom(), false));
        g.fillPath(p);
        g.saveState();
        g.reduceClipRegion(p);
        g.setColour(Colours::white.withAlpha(0.06f));
        for (float x = r.getX() + 2; x < r.getRight(); x += 3.0f) g.drawVerticalLine((int) x, r.getY(), r.getBottom());
        g.restoreState();
        return;
    }
    if (segment) {
        if (highlighted) g.setColour(Colours::white.withAlpha(down ? 0.06f : 0.035f)), g.fillRoundedRectangle(r.reduced(1.0f), 6.0f);
        return;
    }
    g.setColour(colours::panel.brighter(highlighted ? 0.25f : 0.1f));
    g.fillRoundedRectangle(r.reduced(1.0f), 10.0f);
    g.setColour(Colours::white.withAlpha(highlighted ? 0.12f : 0.06f));
    g.drawRoundedRectangle(r.reduced(1.5f), 10.0f, 1.0f);
}

void Look::drawButtonText(Graphics& g, TextButton& b, bool highlighted, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? Colour(0xff15204a) : highlighted ? colours::ink : colours::dim);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(6, 2), Justification::centred, 1);
}

// ---- art ------------------------------------------------------------------------------------------

namespace art {

void panel(Graphics& g, Rectangle<float> r, float corner) {
    Path p;
    p.addRoundedRectangle(r, corner);
    DropShadow(Colours::black.withAlpha(0.45f), 24, {0, 8}).drawForPath(g, p);
    g.setGradientFill(ColourGradient(Colour(0xff171b36), r.getX(), r.getY(), Colour(0xff11152a), r.getX(), r.getBottom(), false));
    g.fillPath(p);
    g.setColour(Colours::white.withAlpha(0.045f));
    g.strokePath(p, PathStrokeType(1.0f));
}

void photo(Graphics& g, const Image& img, Rectangle<float> r, float corner) {
    Path clip;
    clip.addRoundedRectangle(r, corner);
    g.saveState();
    g.reduceClipRegion(clip);
    g.setOpacity(1.0f); // images take the alpha of the current colour
    g.drawImage(img, r, RectanglePlacement::centred | RectanglePlacement::fillDestination);
    g.restoreState();
    g.setColour(Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
}

void cheek(Graphics& g, const Image& wood, Rectangle<float> r, bool left) {
    // a walnut end cheek, oiled, its inner edge in shadow where it meets the panel
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    g.setOpacity(1.0f); // images take the alpha of the current colour
    g.drawImage(wood, r, RectanglePlacement::centred | RectanglePlacement::fillDestination);
    const float inner = left ? r.getRight() : r.getX(), outer = left ? r.getX() : r.getRight();
    ColourGradient shade(Colours::black.withAlpha(0.65f), inner, 0, Colours::black.withAlpha(0.35f), outer, 0, false);
    shade.addColour(0.55, Colours::white.withAlpha(0.06f));
    shade.addColour(0.8, Colours::transparentBlack);
    g.setGradientFill(shade);
    g.fillRect(r);
    g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.08f), 0, r.getY(), Colours::black.withAlpha(0.35f), 0, r.getBottom(), false));
    g.fillRect(r);
    g.restoreState();
}

void screw(Graphics& g, juce::Point<float> c, float radius, float angle) {
    // a dark, brushed hex-socket screw sunk into the panel
    g.setColour(Colours::black.withAlpha(0.6f));
    g.fillEllipse(c.x - radius - 1.5f, c.y - radius - 0.5f, radius * 2 + 3, radius * 2 + 3);
    g.setGradientFill(ColourGradient(Colour(0xff8e94ad), c.x - radius, c.y - radius, Colour(0xff2a2f45), c.x + radius, c.y + radius, false));
    g.fillEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
    Path hex;
    for (int i = 0; i < 6; ++i) {
        const auto p = polar(c, radius * 0.45f, angle + (float) i * MathConstants<float>::pi / 3.0f);
        i == 0 ? hex.startNewSubPath(p) : hex.lineTo(p);
    }
    hex.closeSubPath();
    g.setColour(Colour(0xff0b0d17));
    g.fillPath(hex);
    g.setColour(Colours::white.withAlpha(0.25f));
    g.drawEllipse(c.x - radius + 0.5f, c.y - radius + 0.5f, radius * 2 - 1, radius * 2 - 1, 0.8f);
}

void glow(Graphics& g, const Path& p, float strength) {
    DropShadow(colours::blue.withAlpha(0.55f * strength), 22, {}).drawForPath(g, p);
}

void meter(Graphics& g, Rectangle<float> r, float level, float peak) {
    g.setColour(colours::well);
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    const float w = (r.getWidth() - 4) * jlimit(0.0f, 1.0f, level);
    if (w > 1) {
        auto bar = Rectangle<float>(r.getX() + 2, r.getY() + 2, w, r.getHeight() - 4);
        ColourGradient grad(colours::blue, r.getX(), 0, colours::cyan, r.getRight(), 0, false);
        grad.addColour(0.86, colours::cyan);
        grad.addColour(0.97, colours::red);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bar, (r.getHeight() - 4) * 0.5f);
    }
    if (peak > 0.01f) {
        g.setColour(colours::ink);
        g.fillRect(r.getX() + 2 + jlimit(0.0f, 1.0f, peak) * (r.getWidth() - 6), r.getY() + 2, 2.0f, r.getHeight() - 4);
    }
}

} // namespace art
} // namespace ui
