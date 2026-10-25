// AlptugDUB by Alptuğ Dağ — the look: walnut, brushed steel, machined knobs, a lit condenser mic.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

namespace colours {
inline const juce::Colour cream{0xfff3e9d2};
inline const juce::Colour ink{0xfff5f1ea};
inline const juce::Colour dim{0xffb9ad9a};
inline const juce::Colour faint{0xff7d7266};
inline const juce::Colour red{0xffff3b30};    // the "DUB", like CineTV's red "TV"
inline const juce::Colour amber{0xffffb347};  // lamps and value arcs
inline const juce::Colour plate{0xff1b1a1d};
} // namespace colours

/** Poppins, the CineTV typeface, bundled. */
juce::Font font(float height, int weight = 600); // 500 medium, 700 bold, 900 black

class Look : public juce::LookAndFeel_V4 {
public:
    Look();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return font((float) h * 0.38f, 600); }
    juce::Font getLabelFont(juce::Label&) override { return font(13.0f, 500); }
};

namespace art {
/** Oiled walnut, generated once per size. */
juce::Image wood(int w, int h);
/** A brushed, dark-anodised face plate with four screws. */
void plate(juce::Graphics&, juce::Rectangle<float> r);
void screw(juce::Graphics&, juce::Point<float> c, float radius, float angle);
/** A large-diaphragm condenser microphone in 3D; `level` (0..1) lights its capsule. */
void microphone(juce::Graphics&, juce::Rectangle<float> r, float level);
/** An analog VU meter; `needle` is 0..1 across the scale. */
void vu(juce::Graphics&, juce::Rectangle<float> r, float needle, const juce::String& label);
} // namespace art

} // namespace ui
