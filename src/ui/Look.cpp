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

Look::Look() {
    setColour(Label::textColourId, colours::ink);
    setColour(TooltipWindow::backgroundColourId, colours::plate);
    setColour(TooltipWindow::textColourId, colours::ink);
}

// ---- knobs ------------------------------------------------------------------------------------------

void Look::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s) {
    const float size = (float) jmin(w, h) - 6.0f;
    const auto c = Rectangle<float>((float) x, (float) y, (float) w, (float) h).getCentre();
    const float r = size * 0.5f;
    const float angle = start + pos * (end - start);
    const bool hover = s.isMouseOverOrDragging();

    // drop shadow on the plate
    for (int i = 6; i > 0; --i)
        g.setColour(Colours::black.withAlpha(0.06f)), g.fillEllipse(c.x - r - i * 0.3f, c.y - r + i * 0.9f, 2 * r + i * 0.6f, 2 * r + i * 0.6f);

    // scale: ticks and the value arc
    const float scaleR = r * 1.02f;
    for (int i = 0; i <= 10; ++i) {
        const float a = start + (end - start) * (float) i / 10.0f;
        const float len = i % 5 == 0 ? 0.13f : 0.08f;
        g.setColour(colours::cream.withAlpha(i % 5 == 0 ? 0.55f : 0.30f));
        g.drawLine(c.x + std::sin(a) * scaleR * (1.0f + len * 0.2f), c.y - std::cos(a) * scaleR * (1.0f + len * 0.2f),
                   c.x + std::sin(a) * scaleR * (1.0f + len + 0.06f), c.y - std::cos(a) * scaleR * (1.0f + len + 0.06f), 1.2f);
    }
    {
        // bipolar parameters light the arc from their middle
        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float from = bipolar ? start + (end - start) * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : start;
        Path arc;
        arc.addCentredArc(c.x, c.y, scaleR * 0.94f, scaleR * 0.94f, 0, jmin(from, angle), jmax(from, angle), true);
        g.setColour(colours::amber.withAlpha(0.25f));
        g.strokePath(arc, PathStrokeType(r * 0.16f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(colours::amber);
        g.strokePath(arc, PathStrokeType(r * 0.055f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // skirt: dark, knurled edge
    const float kr = r * 0.78f;
    g.setGradientFill(ColourGradient(Colour(0xff2b2a2e), c.x, c.y - kr, Colour(0xff0c0c0e), c.x, c.y + kr, false));
    g.fillEllipse(c.x - kr, c.y - kr, 2 * kr, 2 * kr);
    for (int i = 0; i < 72; ++i) {
        const float a = (float) i / 72.0f * MathConstants<float>::twoPi + angle;
        const float light = 0.5f + 0.5f * std::cos(a - 0.9f);
        g.setColour(Colours::white.withAlpha(0.05f + 0.18f * light * light));
        g.drawLine(c.x + std::sin(a) * kr * 0.90f, c.y - std::cos(a) * kr * 0.90f, c.x + std::sin(a) * kr * 0.995f,
                   c.y - std::cos(a) * kr * 0.995f, 1.1f);
    }
    // machined aluminium cap with concentric brushing
    const float cr = kr * 0.80f;
    ColourGradient cap(Colour(0xfff4f1ec), c.x - cr * 0.35f, c.y - cr * 0.6f, Colour(0xff6b6962), c.x + cr * 0.5f, c.y + cr, true);
    cap.addColour(0.45, Colour(0xffc9c5bd));
    g.setGradientFill(cap);
    g.fillEllipse(c.x - cr, c.y - cr, 2 * cr, 2 * cr);
    for (float rr = cr * 0.15f; rr < cr; rr += cr * 0.07f)
        g.setColour(Colours::black.withAlpha(0.035f)), g.drawEllipse(c.x - rr, c.y - rr, 2 * rr, 2 * rr, 0.8f);
    g.setColour(Colours::white.withAlpha(0.55f));
    g.drawEllipse(c.x - cr, c.y - cr, 2 * cr, 2 * cr, 1.0f);
    // pointer
    Path p;
    p.addRoundedRectangle(-cr * 0.055f, -cr * 0.95f, cr * 0.11f, cr * 0.48f, cr * 0.05f);
    g.setColour(hover ? colours::red : Colour(0xff1d1c20));
    g.fillPath(p, AffineTransform::rotation(angle).translated(c));
}

// ---- switches and buttons ---------------------------------------------------------------------------

void Look::drawToggleButton(Graphics& g, ToggleButton& b, bool highlighted, bool) {
    auto r = b.getLocalBounds().toFloat();
    const float h = jmin(r.getHeight(), 22.0f);
    auto track = Rectangle<float>(r.getX() + 2, r.getCentreY() - h / 2, h * 1.9f, h);
    const bool on = b.getToggleState();
    g.setColour(Colour(0xff0e0e10));
    g.fillRoundedRectangle(track, h / 2);
    g.setColour(on ? colours::amber.withAlpha(0.85f) : Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(track.reduced(0.5f), h / 2, 1.0f);
    const float kx = on ? track.getRight() - h + 2 : track.getX() + 2;
    auto knob = Rectangle<float>(kx, track.getY() + 2, h - 4, h - 4);
    g.setGradientFill(ColourGradient(Colour(0xfff2efe9), knob.getX(), knob.getY(), Colour(0xff8f8c84), knob.getRight(), knob.getBottom(), false));
    g.fillEllipse(knob);
    if (on) {
        g.setColour(colours::amber.withAlpha(0.35f));
        g.fillEllipse(knob.expanded(4));
        g.setColour(colours::amber);
        g.fillEllipse(knob.reduced(knob.getWidth() * 0.32f));
    }
    g.setColour(highlighted ? colours::ink : colours::dim);
    g.setFont(font(13.0f, 600));
    g.drawText(b.getButtonText(), Rectangle<float>(track.getRight() + 10, r.getY(), r.getRight() - track.getRight() - 10, r.getHeight()),
               Justification::centredLeft);
}

void Look::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool highlighted, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = b.getToggleState();
    const float corner = r.getHeight() * 0.32f;
    if (on) {
        g.setColour(colours::amber.withAlpha(0.25f));
        g.fillRoundedRectangle(r.expanded(1.5f), corner + 1);
        g.setGradientFill(ColourGradient(Colour(0xffffcf86), r.getX(), r.getY(), Colour(0xffe8902a), r.getX(), r.getBottom(), false));
    } else {
        g.setGradientFill(ColourGradient(Colour(0xff2a2826).withAlpha(0.95f), r.getX(), r.getY(), Colour(0xff151413), r.getX(), r.getBottom(), false));
    }
    g.fillRoundedRectangle(r, corner);
    g.setColour(on ? Colours::white.withAlpha(0.5f) : Colours::white.withAlpha(highlighted ? 0.22f : 0.09f));
    g.drawRoundedRectangle(r, corner, 1.0f);
    if (down) g.setColour(Colours::black.withAlpha(0.15f)), g.fillRoundedRectangle(r, corner);
}

void Look::drawButtonText(Graphics& g, TextButton& b, bool highlighted, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? Colour(0xff1b1206) : highlighted ? colours::ink : colours::dim);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(6, 2), Justification::centred, 2);
}

// ---- art ------------------------------------------------------------------------------------------

namespace art {

static float valueNoise(float x, float y) {
    auto hash = [](int a, int b) {
        uint32_t h = (uint32_t) a * 374761393u + (uint32_t) b * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) (h & 0xffff) / 65535.0f;
    };
    const int xi = (int) std::floor(x), yi = (int) std::floor(y);
    const float fx = x - xi, fy = y - yi;
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

Image wood(int w, int h) {
    Image img(Image::RGB, w, h, false);
    Image::BitmapData px(img, Image::BitmapData::writeOnly);
    const Colour dark(0xff2a170d), mid(0xff5b3620), light(0xff8a5a36);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // long vertical grain that wanders, with knots of figure and fine pores
            const float warp = 14.0f * valueNoise(x * 0.004f, y * 0.0025f) + 5.0f * valueNoise(x * 0.02f, y * 0.01f);
            const float rings = 0.5f + 0.5f * std::sin((x + warp * 6.0f) * 0.11f + valueNoise(x * 0.01f, y * 0.004f) * 9.0f);
            const float pores = valueNoise(x * 0.9f, y * 0.05f);
            float t = std::pow(rings, 1.8f) * 0.75f + pores * 0.25f;
            const Colour base = t < 0.5f ? dark.interpolatedWith(mid, t * 2.0f) : mid.interpolatedWith(light, (t - 0.5f) * 2.0f);
            // vignette and lacquer sheen
            const float vx = (float) x / w - 0.5f, vy = (float) y / h - 0.5f;
            const float shade = 1.0f - 0.55f * (vx * vx + vy * vy) * 1.6f;
            px.setPixelColour(x, y, base.withMultipliedBrightness(jlimit(0.4f, 1.0f, shade)));
        }
    }
    return img;
}

void screw(Graphics& g, Point<float> c, float radius, float angle) {
    g.setColour(Colours::black.withAlpha(0.45f));
    g.fillEllipse(c.x - radius, c.y - radius + 1.2f, radius * 2, radius * 2);
    g.setGradientFill(ColourGradient(Colour(0xffe3ded4), c.x - radius, c.y - radius, Colour(0xff57544e), c.x + radius, c.y + radius, false));
    g.fillEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
    g.setColour(Colour(0xff2a2824));
    const float dx = std::cos(angle) * radius * 0.75f, dy = std::sin(angle) * radius * 0.75f;
    g.drawLine(c.x - dx, c.y - dy, c.x + dx, c.y + dy, radius * 0.28f);
}

void plate(Graphics& g, Rectangle<float> r) {
    // the plate sits in the wood: a soft shadow first
    for (int i = 10; i > 0; --i)
        g.setColour(Colours::black.withAlpha(0.045f)), g.fillRoundedRectangle(r.expanded((float) i).translated(0, (float) i * 0.6f), 18.0f + i);
    g.setGradientFill(ColourGradient(Colour(0xff222125), r.getX(), r.getY(), Colour(0xff121114), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 16.0f);
    // brushing
    Random rnd(7);
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    for (float y = r.getY(); y < r.getBottom(); y += 1.0f) {
        g.setColour(Colours::white.withAlpha(0.008f + rnd.nextFloat() * 0.022f));
        g.drawHorizontalLine((int) y, r.getX(), r.getRight());
    }
    g.restoreState();
    g.setColour(Colours::white.withAlpha(0.13f));
    g.drawRoundedRectangle(r.reduced(0.5f), 16.0f, 1.0f);
    const float s = 7.0f, in = 16.0f;
    screw(g, {r.getX() + in, r.getY() + in}, s, 0.6f);
    screw(g, {r.getRight() - in, r.getY() + in}, s, 2.2f);
    screw(g, {r.getX() + in, r.getBottom() - in}, s, 1.4f);
    screw(g, {r.getRight() - in, r.getBottom() - in}, s, 0.2f);
}

void microphone(Graphics& g, Rectangle<float> r, float level) {
    const float cx = r.getCentreX();
    const float hw = r.getWidth() * 0.60f, hh = r.getHeight() * 0.47f;
    auto head = Rectangle<float>(cx - hw / 2, r.getY() + r.getHeight() * 0.03f, hw, hh);
    const float bw = hw * 0.60f, bh = r.getHeight() * 0.33f;
    auto body = Rectangle<float>(cx - bw / 2, head.getBottom() - hh * 0.04f, bw, bh);

    // shadow on the plate
    for (int i = 8; i > 0; --i)
        g.setColour(Colours::black.withAlpha(0.05f)),
            g.fillEllipse(cx - bw * (0.9f + i * 0.08f), r.getBottom() - 16 - i, bw * (1.8f + i * 0.16f), 14.0f + i * 1.5f);

    // shock-mount yoke behind the body
    {
        Path yoke;
        const float yw = hw * 0.92f, top = head.getCentreY() + hh * 0.18f, bottom = body.getBottom() + bh * 0.12f;
        yoke.startNewSubPath(cx - yw / 2, top);
        yoke.lineTo(cx - yw / 2, bottom - yw * 0.25f);
        yoke.quadraticTo(cx - yw / 2, bottom, cx, bottom);
        yoke.quadraticTo(cx + yw / 2, bottom, cx + yw / 2, bottom - yw * 0.25f);
        yoke.lineTo(cx + yw / 2, top);
        g.setColour(Colour(0xff111013));
        g.strokePath(yoke, PathStrokeType(9.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(Colours::white.withAlpha(0.18f));
        g.strokePath(yoke, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation(-1.5f, -1.5f));
        for (float side : {-1.0f, 1.0f}) {
            g.setGradientFill(ColourGradient(Colour(0xffd8d3ca), cx + side * yw / 2 - 6, top - 6, Colour(0xff4a4741), cx + side * yw / 2 + 6, top + 6, false));
            g.fillEllipse(cx + side * yw / 2 - 8, top - 8, 16, 16);
        }
        // the stand under the yoke
        g.setColour(Colour(0xff111013));
        g.fillRoundedRectangle(cx - 5, bottom - 2, 10, r.getBottom() - bottom - 12, 4);
    }

    // body: champagne-gold cylinder
    {
        ColourGradient cyl(Colour(0xff6e5631), body.getX(), 0, Colour(0xff5a4626), body.getRight(), 0, false);
        cyl.addColour(0.22, Colour(0xfff3dfb0));
        cyl.addColour(0.40, Colour(0xffc9a96a));
        cyl.addColour(0.75, Colour(0xff8a6c3c));
        g.setGradientFill(cyl);
        g.fillRoundedRectangle(body, bw * 0.16f);
        // engraved initials and the "on air" lamp
        g.setColour(Colour(0xff3d2f19).withAlpha(0.75f));
        g.setFont(font(bw * 0.20f, 900));
        g.drawText("AD", body.withTrimmedTop(bh * 0.18f).withHeight(bw * 0.3f), Justification::centred);
        const float lr = bw * 0.065f;
        const Point<float> lamp(cx, body.getY() + bh * 0.62f);
        g.setColour(colours::red.withAlpha(0.15f + 0.6f * level));
        g.fillEllipse(lamp.x - lr * 2.4f, lamp.y - lr * 2.4f, lr * 4.8f, lr * 4.8f);
        g.setColour(colours::red.interpolatedWith(Colour(0xff4a0f0b), 1.0f - jlimit(0.2f, 1.0f, level * 2.0f)));
        g.fillEllipse(lamp.x - lr, lamp.y - lr, lr * 2, lr * 2);
        // a darker base cap
        g.setGradientFill(ColourGradient(Colour(0xff2b2925), body.getX(), 0, Colour(0xff1a1917), body.getRight(), 0, false));
        g.fillRoundedRectangle(body.withTop(body.getBottom() - bh * 0.14f), bw * 0.12f);
    }

    // capsule head: a chromed cage over a fine mesh, lit from inside by the voice
    {
        Path shape;
        shape.addRoundedRectangle(head, hw * 0.5f);
        g.setGradientFill(ColourGradient(Colour(0xff8f8a80), head.getX(), 0, Colour(0xff5e5a53), head.getRight(), 0, false));
        g.fillPath(shape);
        g.saveState();
        g.reduceClipRegion(shape);
        const auto inner = head.reduced(hw * 0.06f);
        g.setGradientFill(ColourGradient(colours::amber.withAlpha(0.10f + 0.55f * level), inner.getCentreX(), inner.getCentreY(),
                                         Colours::transparentBlack, inner.getCentreX(), inner.getY(), true));
        g.fillRect(inner);
        // the mesh: two sets of fine diagonals
        g.setColour(Colour(0xff1d1c1a).withAlpha(0.55f));
        for (float d = -hh; d < hw + hh; d += 3.6f) {
            g.drawLine(head.getX() + d, head.getY(), head.getX() + d - hh, head.getBottom(), 0.9f);
            g.drawLine(head.getX() + d - hh, head.getY(), head.getX() + d, head.getBottom(), 0.9f);
        }
        // roundness: dark sides, bright left highlight
        ColourGradient round(Colours::black.withAlpha(0.65f), head.getX(), 0, Colours::black.withAlpha(0.55f), head.getRight(), 0, false);
        round.addColour(0.28, Colours::white.withAlpha(0.28f));
        round.addColour(0.45, Colours::transparentBlack);
        round.addColour(0.70, Colours::black.withAlpha(0.15f));
        g.setGradientFill(round);
        g.fillPath(shape);
        g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.35f), head.getX() + hw * 0.3f, head.getY() + hh * 0.12f,
                                         Colours::transparentWhite, head.getX() + hw * 0.3f, head.getY() + hh * 0.45f, true));
        g.fillEllipse(head.getX() + hw * 0.12f, head.getY() + hh * 0.05f, hw * 0.38f, hh * 0.35f);
        g.restoreState();
        // chrome cage: rim and a centre band
        g.setColour(Colour(0xffd9d5cc));
        g.strokePath(shape, PathStrokeType(3.0f));
        auto band = Rectangle<float>(head.getX() - 1, head.getCentreY() - 6, hw + 2, 12);
        ColourGradient chrome(Colour(0xff6f6b63), band.getX(), 0, Colour(0xff55524c), band.getRight(), 0, false);
        chrome.addColour(0.25, Colour(0xfffaf8f3));
        chrome.addColour(0.5, Colour(0xffa9a59c));
        g.setGradientFill(chrome);
        g.fillRoundedRectangle(band, 5);
        auto neck = Rectangle<float>(cx - bw * 0.56f, head.getBottom() - 9, bw * 1.12f, 12);
        g.setGradientFill(chrome);
        g.fillRoundedRectangle(neck, 5);
    }
}

void vu(Graphics& g, Rectangle<float> r, float needle, const String& label) {
    // bezel
    g.setColour(Colour(0xff0c0b0d));
    g.fillRoundedRectangle(r, 10);
    auto face = r.reduced(5);
    g.setGradientFill(ColourGradient(Colour(0xfffbf3df), face.getX(), face.getY(), Colour(0xffe4d3ad), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle(face, 7);
    // warm lamp behind the face
    g.setGradientFill(ColourGradient(colours::amber.withAlpha(0.22f), face.getCentreX(), face.getBottom(), Colours::transparentWhite,
                                     face.getCentreX(), face.getY(), true));
    g.fillRoundedRectangle(face, 7);

    const Point<float> pivot(face.getCentreX(), face.getBottom() + face.getHeight() * 0.25f);
    const float rad = face.getHeight() * 1.05f;
    const float a0 = -0.85f, a1 = 0.85f; // radians from vertical
    auto at = [&](float t, float rr) {
        const float a = a0 + (a1 - a0) * t;
        return Point<float>(pivot.x + std::sin(a) * rr, pivot.y - std::cos(a) * rr);
    };
    // the classic VU scale: -20 .. +3, red from 0
    const float marks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    auto tOf = [](float db) {
        const float lo = std::pow(10.0f, -20.0f / 20.0f), hi = std::pow(10.0f, 3.0f / 20.0f);
        return (std::pow(10.0f, db / 20.0f) - lo) / (hi - lo);
    };
    Path red;
    red.addCentredArc(pivot.x, pivot.y, rad * 0.86f, rad * 0.86f, 0, a0 + (a1 - a0) * tOf(0), a1, true);
    g.setColour(Colour(0xffc8261d));
    g.strokePath(red, PathStrokeType(4.0f));
    Path arc;
    arc.addCentredArc(pivot.x, pivot.y, rad * 0.86f, rad * 0.86f, 0, a0, a0 + (a1 - a0) * tOf(0), true);
    g.setColour(Colour(0xff2a241b));
    g.strokePath(arc, PathStrokeType(1.4f));
    g.setFont(font(face.getHeight() * 0.11f, 600));
    for (float m : marks) {
        const float t = tOf(m);
        g.setColour(m >= 0 ? Colour(0xffc8261d) : Colour(0xff2a241b));
        g.drawLine(Line<float>(at(t, rad * 0.86f), at(t, rad * 0.95f)), 1.3f);
        if (m == -20 || m == -10 || m == -5 || m == -3 || m == 0 || m == 3) {
            auto p = at(t, rad * 1.04f);
            g.drawText(m > 0 ? "+" + String((int) m) : String((int) m), Rectangle<float>(p.x - 14, p.y - 8, 28, 14), Justification::centred);
        }
    }
    g.setColour(Colour(0xff2a241b));
    g.setFont(font(face.getHeight() * 0.17f, 900));
    g.drawText("VU", face.withTrimmedTop(face.getHeight() * 0.45f).withHeight(face.getHeight() * 0.22f), Justification::centred);
    g.setFont(font(face.getHeight() * 0.10f, 600));
    g.setColour(Colour(0xff6b5f4b));
    g.drawText(label, face.withTrimmedTop(face.getHeight() * 0.68f).withHeight(face.getHeight() * 0.16f), Justification::centred);

    // needle
    g.saveState();
    g.reduceClipRegion(face.toNearestInt());
    const auto tip = at(jlimit(0.0f, 1.05f, needle), rad * 0.97f);
    g.setColour(Colours::black.withAlpha(0.18f));
    g.drawLine(Line<float>(pivot.translated(2, 2), tip.translated(2, 2)), 2.0f);
    g.setColour(Colour(0xff15120e));
    g.drawLine(Line<float>(pivot, tip), 1.6f);
    g.restoreState();
    // glass
    g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.35f), face.getX(), face.getY(), Colours::transparentWhite, face.getX(),
                                     face.getCentreY(), false));
    g.fillRoundedRectangle(face.withHeight(face.getHeight() * 0.45f), 7);
}

} // namespace art
} // namespace ui
