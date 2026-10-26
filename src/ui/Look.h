// AlptugDUB by Alptuğ Dağ — the look: deep navy, soft depth, knobs and selectors lit in blue.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

namespace colours {
inline const juce::Colour bg{0xff0c0f1d};
inline const juce::Colour panel{0xff141831};
inline const juce::Colour well{0xff0a0d1a};
inline const juce::Colour knob{0xff232849};
inline const juce::Colour ink{0xffe6e9f7};
inline const juce::Colour dim{0xff8a90b4};
inline const juce::Colour faint{0xff4d5378};
inline const juce::Colour blue{0xff1e7bff};
inline const juce::Colour cyan{0xff19b8ff};
inline const juce::Colour red{0xffff3b30}; // the "DUB", like CineTV's red "TV"
} // namespace colours

/** Poppins, the CineTV typeface, bundled. */
juce::Font font(float height, int weight = 600); // 500 medium, 700 bold, 900 black
/** Upper case that knows Turkish letters; setTurkish(true) capitalises i as İ. */
void setTurkish(bool turkish);
juce::String upper(const juce::String&);
/** Upper-case, widely spaced captions. */
void caption(juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float height = 13.0f,
             juce::Colour = colours::dim);

class Look : public juce::LookAndFeel_V4 {
public:
    Look();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return font(juce::jmin(15.0f, (float) h * 0.42f), 600); }
    juce::Font getPopupMenuFont() override { return font(15.0f, 600); }
    void drawBubble(juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
};

namespace art {
/** A soft raised panel. */
void panel(juce::Graphics&, juce::Rectangle<float> r, float corner = 16.0f);
/** A photograph clipped to a rounded frame. */
void photo(juce::Graphics&, const juce::Image&, juce::Rectangle<float> r, float corner = 12.0f);
/** A walnut end cheek on the left or right edge of the instrument. */
void cheek(juce::Graphics&, const juce::Image& wood, juce::Rectangle<float> r, bool left);
/** A dark hex-socket panel screw. */
void screw(juce::Graphics&, juce::Point<float> c, float radius, float angle);
/** A blue glow behind a shape. */
void glow(juce::Graphics&, const juce::Path&, float strength = 1.0f);
/** A horizontal level meter, 0..1. */
void meter(juce::Graphics&, juce::Rectangle<float> r, float level, float peak);
} // namespace art

} // namespace ui
