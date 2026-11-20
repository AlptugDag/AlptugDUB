// AlptugDUB by Alptuğ Dağ — Liquid Glass + Walnut Wood + Tactile Analog Hardware Look.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

namespace colours {
inline const juce::Colour bg{0xff0b0d12};
inline const juce::Colour chassis{0xff12151d};
inline const juce::Colour panel{0xff181c26};
inline const juce::Colour well{0xff07090d};
inline const juce::Colour knob{0xff222733};
inline const juce::Colour ink{0xfff4efe6};
inline const juce::Colour dim{0xff9aa3b2};
inline const juce::Colour faint{0xff596272};
inline const juce::Colour amber{0xffffb340};  // warm analog amber LED & filament
inline const juce::Colour gold{0xffffd28a};   // champagne highlight
inline const juce::Colour cyan{0xff38e8ff};   // liquid glass refraction cyan
inline const juce::Colour bronze{0xffb98b56}; // brushed bronze trim
inline const juce::Colour red{0xffff453a};
} // namespace colours

/** Poppins, the CineTV typeface, bundled. */
juce::Font font(float height, int weight = 600); // 500 medium, 700 bold, 900 black
/** Upper case that knows Turkish letters; setTurkish(true) capitalises i as İ. */
void setTurkish(bool turkish);
juce::String upper(const juce::String&);
/** Spaced hardware engraving caption. */
void caption(juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float height = 11.5f,
             juce::Colour = colours::dim);

class Look : public juce::LookAndFeel_V4 {
public:
    Look();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return font(juce::jmin(13.5f, (float) h * 0.40f), 600); }
    juce::Font getPopupMenuFont() override { return font(14.0f, 500); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(13.0f, 600); }
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawBubble(juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
};

namespace art {
/** Translucent smoked Liquid Glass panel with specular top refraction and subtle warm/cool rim. */
void glassPanel(juce::Graphics&, juce::Rectangle<float> r, float corner = 16.0f, float alpha = 0.72f);
/** Curved Liquid Glass oscilloscope lens with diagonal glass gloss reflection. */
void glassLens(juce::Graphics&, juce::Rectangle<float> r, float corner = 18.0f);
/** Oiled walnut wood end cheek with 3D beveled shadow. */
void cheek(juce::Graphics&, const juce::Image& wood, juce::Rectangle<float> r, bool left);
/** Sunk brushed hex-socket panel screw. */
void screw(juce::Graphics&, juce::Point<float> c, float radius, float angle);
/** A photograph clipped to a rounded frame. */
void photo(juce::Graphics&, const juce::Image&, juce::Rectangle<float> r, float corner = 12.0f);
/** Soft luminous glow behind a path. */
void glow(juce::Graphics&, const juce::Path&, float strength = 1.0f, juce::Colour col = colours::amber);
/** Jewel LED lamp with specular glass dot. */
void jewelLed(juce::Graphics&, juce::Point<float> centre, float radius, bool on, juce::Colour col = colours::amber);
/** Warm analog glass bar meter, 0..1. */
void meter(juce::Graphics&, juce::Rectangle<float> r, float level, float peak);
} // namespace art

} // namespace ui
