// AlptugDUB by Alptuğ Dağ — Liquid Glass + Walnut Wood + Tactile Analog Hardware Look.
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
    g.setFont(font(height, 500).withExtraKerningFactor(0.18f));
    g.drawText(upper(text), r, j, false);
}

Look::Look() {
    setColour(TooltipWindow::backgroundColourId, colours::panel);
    setColour(TooltipWindow::textColourId, colours::ink);
    setColour(TooltipWindow::outlineColourId, colours::amber.withAlpha(0.30f));
    setColour(PopupMenu::backgroundColourId, colours::panel);
    setColour(PopupMenu::textColourId, colours::ink);
    setColour(PopupMenu::highlightedBackgroundColourId, colours::amber.withAlpha(0.25f));
    setColour(PopupMenu::highlightedTextColourId, colours::ink);
    setColour(BubbleComponent::backgroundColourId, colours::panel);
    setColour(BubbleComponent::outlineColourId, colours::amber.withAlpha(0.30f));
}

void Look::drawBubble(Graphics& g, BubbleComponent&, const juce::Point<float>&, const Rectangle<float>& body) {
    g.setColour(colours::panel);
    g.fillRoundedRectangle(body, 8);
    g.setColour(colours::amber.withAlpha(0.45f));
    g.drawRoundedRectangle(body.reduced(0.5f), 8, 1);
}

namespace {
juce::Point<float> polar(juce::Point<float> c, float r, float a) { return {c.x + std::sin(a) * r, c.y - std::cos(a) * r}; }
Colour dialCol(float t) { return colours::amber.interpolatedWith(colours::cyan, jlimit(0.0f, 1.0f, t)); }
} // namespace

// ---- realistic 3D knurled analog knobs --------------------------------------------------------------

void Look::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s) {
    const auto box = Rectangle<float>((float) x, (float) y, (float) w, (float) h);
    const auto c = box.getCentre();
    const float R = jmin(box.getWidth(), box.getHeight()) * 0.5f - 3.0f;
    const bool big = R > 52.0f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? start + (end - start) * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : start;
    const float lo = jmin(from, angle), hi = jmax(from, angle);

    // 1. Engraved outer tick ring on chassis faceplate
    const int ticks = big ? 33 : 21;
    const float tickOut = R;
    const float tickIn = R - (big ? 5.5f : 3.5f);
    for (int i = 0; i < ticks; ++i) {
        const float u = (float) i / (float) (ticks - 1);
        const float a = start + u * (end - start);
        const bool lit = a >= lo - 0.02f && a <= hi + 0.02f && (hi - lo > 0.015f);
        const auto p0 = polar(c, tickIn, a);
        const auto p1 = polar(c, tickOut, a);
        if (lit) {
            const Colour col = dialCol(u);
            g.setColour(col.withAlpha(0.45f));
            g.drawLine(p0.x, p0.y, p1.x, p1.y, big ? 3.2f : 2.4f);
            g.setColour(col);
            g.drawLine(p0.x, p0.y, p1.x, p1.y, big ? 1.5f : 1.2f);
        } else {
            g.setColour(Colours::white.withAlpha(i % 4 == 0 ? 0.22f : 0.09f));
            g.drawLine(p0.x, p0.y, p1.x, p1.y, 1.0f);
        }
    }

    // 2. Luminous Liquid Glass light-pipe arc behind the knob skirt
    const float arcR = R - (big ? 9.5f : 6.0f);
    Path track;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0, start, end, true);
    g.setColour(Colour(0xff090b0f));
    g.strokePath(track, PathStrokeType(big ? 4.0f : 2.8f, PathStrokeType::curved, PathStrokeType::rounded));
    if (hi - lo > 0.01f) {
        Path litArc;
        litArc.addCentredArc(c.x, c.y, arcR, arcR, 0, lo, hi, true);
        g.setColour(colours::amber.withAlpha(0.28f));
        g.strokePath(litArc, PathStrokeType(big ? 8.0f : 5.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setGradientFill(ColourGradient(colours::amber, polar(c, arcR, lo), colours::cyan, polar(c, arcR, hi), false));
        g.strokePath(litArc, PathStrokeType(big ? 3.2f : 2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // 3. Cast hardware drop shadow under the machined aluminum knob body
    const float kr = R * (big ? 0.75f : 0.74f);
    Path body;
    body.addEllipse(c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
    DropShadow(Colours::black.withAlpha(0.85f), (int) (kr * 0.35f), {0, (int) (kr * 0.16f)}).drawForPath(g, body);

    // 4. Machined 36-tooth knurled grip skirt around outer edge
    const int teeth = big ? 40 : 28;
    g.setColour(Colour(0xff101319));
    g.fillPath(body);
    for (int i = 0; i < teeth; ++i) {
        const float a = angle + (float) i * MathConstants<float>::twoPi / (float) teeth;
        const float shade = 0.5f + 0.5f * std::sin(a - 0.8f);
        const auto p0 = polar(c, kr * 0.86f, a);
        const auto p1 = polar(c, kr * 0.99f, a);
        g.setColour(Colours::white.withAlpha(0.03f + 0.13f * shade));
        g.drawLine(p0.x, p0.y, p1.x, p1.y, big ? 2.2f : 1.6f);
    }

    // 5. Brushed dark gunmetal top cap with subtle Liquid Glass specular rim
    const float capR = kr * 0.85f;
    g.setGradientFill(ColourGradient(Colour(0xff2c3240), c.x - capR * 0.5f, c.y - capR * 0.7f, Colour(0xff13171f), c.x + capR * 0.5f, c.y + capR, false));
    g.fillEllipse(c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);

    // Anisotropic top-left metallic sheen & warm lower reflection
    ColourGradient sheen(Colours::white.withAlpha(0.20f), c.x - capR * 0.7f, c.y - capR * 0.7f, Colours::transparentWhite, c.x, c.y, false);
    g.setGradientFill(sheen);
    g.fillEllipse(c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);

    g.setColour(Colours::white.withAlpha(0.14f));
    g.drawEllipse(c.x - capR + 0.5f, c.y - capR + 0.5f, capR * 2.0f - 1.0f, capR * 2.0f - 1.0f, 1.0f);

    // 6. Engraved luminous pointer groove
    const auto n0 = polar(c, capR * 0.25f, angle);
    const auto n1 = polar(c, capR * 0.88f, angle);
    g.setColour(colours::amber.withAlpha(s.isMouseOverOrDragging() ? 0.55f : 0.30f));
    g.drawLine(n0.x, n0.y, n1.x, n1.y, big ? 5.5f : 4.2f);
    g.setColour(colours::ink);
    g.drawLine(n0.x, n0.y, n1.x, n1.y, big ? 2.4f : 1.8f);
}

// ---- tactile physical analog pushbuttons & switches -------------------------------------------------

void Look::drawToggleButton(Graphics& g, ToggleButton& b, bool highlighted, bool down) {
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const bool hardwareKey = (bool) b.getProperties()["analog"];

    if (hardwareKey) {
        // Tactile physical analog console keycap inside recessed bezel
        auto slot = r.reduced(0.5f);
        g.setColour(Colour(0xff06070a));
        g.fillRoundedRectangle(slot, 7.0f);
        g.setColour(Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(slot, 7.0f, 1.0f);

        auto cap = slot.reduced(2.0f);
        if (on || down) cap.translate(0.0f, 1.0f);
        g.setGradientFill(ColourGradient(on ? Colour(0xff272c36) : Colour(0xff232732).brighter(highlighted ? 0.08f : 0.0f),
                                         cap.getX(), cap.getY(),
                                         on ? Colour(0xff14171e) : Colour(0xff151922),
                                         cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 5.5f);
        // Top specular keycap edge (3D bevel)
        g.setColour(Colours::white.withAlpha(on ? 0.14f : 0.18f));
        g.drawRoundedRectangle(cap.reduced(0.5f), 5.5f, 1.0f);

        // Jewel LED indicator on the left of the physical keycap
        const Colour ledCol = b.getProperties().contains("cyanLed") ? colours::cyan : colours::amber;
        art::jewelLed(g, {cap.getX() + 14.0f, cap.getCentreY()}, 4.0f, on, ledCol);

        g.setColour(on ? colours::ink : (highlighted ? colours::ink : colours::dim));
        g.setFont(font(11.5f, on ? 600 : 500));
        g.drawText(b.getButtonText(), cap.withTrimmedLeft(25.0f).withTrimmedRight(6.0f), Justification::centredLeft);
        return;
    }

    // Drawer / settings switch (warm amber jewel toggle)
    const float h = jmin(r.getHeight() - 4, 20.0f);
    auto track = Rectangle<float>(r.getX() + 2, r.getCentreY() - h / 2, h * 1.85f, h);
    if (on) {
        Path p;
        p.addRoundedRectangle(track, h / 2);
        art::glow(g, p, 0.5f, colours::amber);
        g.setGradientFill(ColourGradient(colours::amber, track.getX(), 0, colours::gold, track.getRight(), 0, false));
        g.fillPath(p);
    } else {
        g.setColour(colours::well);
        g.fillRoundedRectangle(track, h / 2);
        g.setColour(Colours::white.withAlpha(0.10f));
        g.drawRoundedRectangle(track.reduced(0.5f), h / 2, 1.0f);
    }
    const float kx = on ? track.getRight() - h + 3 : track.getX() + 3;
    g.setColour(on ? Colours::white : colours::dim);
    g.fillEllipse(kx, track.getY() + 3, h - 6, h - 6);
    g.setColour(highlighted || on ? colours::ink : colours::dim);
    g.setFont(font(12.5f, 500));
    g.drawText(b.getButtonText(), Rectangle<float>(track.getRight() + 10, r.getY(), r.getRight() - track.getRight() - 10, r.getHeight()),
               Justification::centredLeft);
}

void Look::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool highlighted, bool down) {
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const bool segment = (bool) b.getProperties()["segment"];

    // Physical recessed hardware socket
    auto slot = r.reduced(0.5f);
    g.setColour(Colour(0xff06070a));
    g.fillRoundedRectangle(slot, 7.0f);

    // 3D beveled keycap inside the socket (presses down 1px when active/down)
    auto cap = slot.reduced(1.5f);
    if (on || down) cap.translate(0.0f, 1.0f);

    if (on) {
        g.setGradientFill(ColourGradient(Colour(0xff2b2722), cap.getX(), cap.getY(), Colour(0xff171513), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 6.0f);
        g.setColour(colours::amber.withAlpha(0.55f));
        g.drawRoundedRectangle(cap.reduced(0.5f), 6.0f, 1.0f);
    } else {
        g.setGradientFill(ColourGradient(Colour(0xff232732).brighter(highlighted ? 0.09f : 0.0f), cap.getX(), cap.getY(),
                                         Colour(0xff141820), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 6.0f);
        g.setColour(Colours::white.withAlpha(highlighted ? 0.18f : 0.11f));
        g.drawRoundedRectangle(cap.reduced(0.5f), 6.0f, 1.0f);
    }

    // Top specular glass/metal lip
    g.setColour(Colours::white.withAlpha(on ? 0.12f : 0.16f));
    g.drawLine(cap.getX() + 6.0f, cap.getY() + 1.0f, cap.getRight() - 6.0f, cap.getY() + 1.0f, 1.0f);

    // For character bank keys ("segment"), add an analog illuminated LED strip bar along the top!
    if (segment) {
        auto bar = Rectangle<float>(cap.getCentreX() - 14.0f, cap.getY() + 4.0f, 28.0f, 3.0f);
        if (on) {
            g.setColour(colours::amber.withAlpha(0.35f));
            g.fillRoundedRectangle(bar.expanded(3.0f, 2.0f), 3.0f);
            g.setColour(colours::amber);
            g.fillRoundedRectangle(bar, 1.5f);
        } else {
            g.setColour(Colour(0xff0b0d12));
            g.fillRoundedRectangle(bar, 1.5f);
        }
    }
}

void Look::drawButtonText(Graphics& g, TextButton& b, bool highlighted, bool down) {
    const bool on = b.getToggleState();
    const bool segment = (bool) b.getProperties()["segment"];
    auto bounds = b.getLocalBounds().reduced(6, 2);
    if (on || down) bounds.translate(0, 1);
    if (segment) bounds.removeFromTop(6);
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(on ? colours::gold : (highlighted ? colours::ink : colours::dim));
    g.drawFittedText(b.getButtonText(), bounds, Justification::centred, 1);
}

// ---- Liquid Glass, Walnut Wood & Hardware Art -------------------------------------------------------

namespace art {

void glassPanel(Graphics& g, Rectangle<float> r, float corner, float alpha) {
    Path p;
    p.addRoundedRectangle(r, corner);
    DropShadow(Colours::black.withAlpha(0.65f), 22, {0, 8}).drawForPath(g, p);

    // Smoked translucent Liquid Glass body
    ColourGradient body(Colour(0xff1e2430).withAlpha(alpha), r.getX(), r.getY(),
                        Colour(0xff10131a).withAlpha(alpha + 0.12f), r.getX(), r.getBottom(), false);
    g.setGradientFill(body);
    g.fillPath(p);

    // Subtle internal top-to-mid liquid sheen
    g.saveState();
    g.reduceClipRegion(p);
    g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.08f), r.getX(), r.getY(),
                                     Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.45f, false));
    g.fillRect(r);
    g.restoreState();

    // Specular glass edge refraction (bright white-gold top, cool cyan bottom hint)
    ColourGradient edge(Colours::white.withAlpha(0.24f), r.getX(), r.getY(),
                        colours::cyan.withAlpha(0.14f), r.getRight(), r.getBottom(), false);
    g.setGradientFill(edge);
    g.strokePath(p, PathStrokeType(1.1f));
}

void glassLens(Graphics& g, Rectangle<float> r, float corner) {
    Path p;
    p.addRoundedRectangle(r, corner);
    DropShadow(Colours::black.withAlpha(0.75f), 26, {0, 10}).drawForPath(g, p);

    // Deep obsidian glass cavity behind the lens
    g.setGradientFill(ColourGradient(Colour(0xff0b0f16), r.getCentreX(), r.getCentreY(), Colour(0xff05070b), r.getX(), r.getBottom(), true));
    g.fillPath(p);

    // Curved Liquid Glass surface sheen sweep
    g.saveState();
    g.reduceClipRegion(p);
    Path gloss;
    gloss.startNewSubPath(r.getX(), r.getY());
    gloss.lineTo(r.getRight(), r.getY());
    gloss.lineTo(r.getRight(), r.getY() + r.getHeight() * 0.28f);
    gloss.quadraticTo(r.getCentreX(), r.getY() + r.getHeight() * 0.48f, r.getX(), r.getY() + r.getHeight() * 0.58f);
    gloss.closeSubPath();
    g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.07f), r.getX(), r.getY(),
                                     Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.58f, false));
    g.fillPath(gloss);
    g.restoreState();

    // Beveled Liquid Glass inner/outer rim
    ColourGradient rim(Colours::white.withAlpha(0.26f), r.getX(), r.getY(),
                       colours::amber.withAlpha(0.22f), r.getRight(), r.getBottom(), false);
    g.setGradientFill(rim);
    g.strokePath(p, PathStrokeType(1.25f));
}

void cheek(Graphics& g, const Image& wood, Rectangle<float> r, bool left) {
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    g.setOpacity(1.0f);
    g.drawImage(wood, r, RectanglePlacement::centred | RectanglePlacement::fillDestination);

    // Warm oiled walnut tint & 3D beveled edge shadows
    const float inner = left ? r.getRight() : r.getX();
    const float outer = left ? r.getX() : r.getRight();
    ColourGradient shade(Colours::black.withAlpha(0.70f), inner, 0, Colours::black.withAlpha(0.32f), outer, 0, false);
    shade.addColour(0.22, colours::amber.withAlpha(0.06f));
    shade.addColour(0.60, Colours::white.withAlpha(0.08f));
    shade.addColour(0.85, Colours::transparentBlack);
    g.setGradientFill(shade);
    g.fillRect(r);

    g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.08f), 0, r.getY(), Colours::black.withAlpha(0.45f), 0, r.getBottom(), false));
    g.fillRect(r);
    g.restoreState();

    // Thin brushed bronze inlay strip where wood meets the chassis
    const float seamX = left ? r.getRight() - 1.5f : r.getX();
    g.setColour(colours::bronze.withAlpha(0.45f));
    g.fillRect(seamX, r.getY(), 1.5f, r.getHeight());
}

void screw(Graphics& g, juce::Point<float> c, float radius, float angle) {
    g.setColour(Colours::black.withAlpha(0.7f));
    g.fillEllipse(c.x - radius - 1.5f, c.y - radius - 0.5f, radius * 2 + 3, radius * 2 + 3);
    g.setGradientFill(ColourGradient(Colour(0xff9aa1ad), c.x - radius, c.y - radius, Colour(0xff2c313b), c.x + radius, c.y + radius, false));
    g.fillEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
    Path hex;
    for (int i = 0; i < 6; ++i) {
        const auto p = polar(c, radius * 0.46f, angle + (float) i * MathConstants<float>::pi / 3.0f);
        i == 0 ? hex.startNewSubPath(p) : hex.lineTo(p);
    }
    hex.closeSubPath();
    g.setColour(Colour(0xff0a0b0e));
    g.fillPath(hex);
    g.setColour(Colours::white.withAlpha(0.26f));
    g.drawEllipse(c.x - radius + 0.5f, c.y - radius + 0.5f, radius * 2 - 1, radius * 2 - 1, 0.8f);
}

void photo(Graphics& g, const Image& img, Rectangle<float> r, float corner) {
    Path clip;
    clip.addRoundedRectangle(r, corner);
    g.saveState();
    g.reduceClipRegion(clip);
    g.setOpacity(1.0f);
    g.drawImage(img, r, RectanglePlacement::centred | RectanglePlacement::fillDestination);
    g.restoreState();
    g.setColour(Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
}

void glow(Graphics& g, const Path& p, float strength, Colour col) {
    DropShadow(col.withAlpha(0.48f * strength), 18, {}).drawForPath(g, p);
}

void jewelLed(Graphics& g, juce::Point<float> c, float radius, bool on, Colour col) {
    // Dark recessed metal bezel
    g.setColour(Colour(0xff050608));
    g.fillEllipse(c.x - radius - 1.5f, c.y - radius - 1.5f, (radius + 1.5f) * 2.0f, (radius + 1.5f) * 2.0f);
    if (on) {
        g.setColour(col.withAlpha(0.32f));
        g.fillEllipse(c.x - radius * 2.4f, c.y - radius * 2.4f, radius * 4.8f, radius * 4.8f);
        g.setGradientFill(ColourGradient(Colours::white.interpolatedWith(col, 0.35f), c.x - radius * 0.4f, c.y - radius * 0.4f,
                                         col, c.x + radius, c.y + radius, false));
        g.fillEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
        // Glass lens specular pinpoint
        g.setColour(Colours::white.withAlpha(0.85f));
        g.fillEllipse(c.x - radius * 0.45f, c.y - radius * 0.45f, radius * 0.6f, radius * 0.6f);
    } else {
        g.setColour(col.withAlpha(0.12f));
        g.fillEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void meter(Graphics& g, Rectangle<float> r, float level, float peak) {
    g.setColour(colours::well);
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    g.setColour(Colours::white.withAlpha(0.06f));
    g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 0.8f);
    const float w = (r.getWidth() - 4.0f) * jlimit(0.0f, 1.0f, level);
    if (w > 1.0f) {
        auto bar = Rectangle<float>(r.getX() + 2.0f, r.getY() + 2.0f, w, r.getHeight() - 4.0f);
        ColourGradient grad(colours::amber, r.getX(), 0, colours::cyan, r.getRight(), 0, false);
        grad.addColour(0.86, colours::cyan);
        grad.addColour(0.97, colours::red);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bar, (r.getHeight() - 4.0f) * 0.5f);
    }
    if (peak > 0.01f) {
        g.setColour(colours::ink);
        g.fillRect(r.getX() + 2.0f + jlimit(0.0f, 1.0f, peak) * (r.getWidth() - 6.0f), r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f);
    }
}

} // namespace art
} // namespace ui
