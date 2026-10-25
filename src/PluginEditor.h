// AlptugDUB by Alptuğ Dağ — the editor.
#pragma once

#include "PluginProcessor.h"
#include "ui/Look.h"

#include <juce_audio_utils/juce_audio_utils.h>

/** A knob with its name above the value. */
class Knob : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& name, const juce::String& tip);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider slider;

private:
    juce::String title;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
};

/** The design-size canvas everything sits on; the editor scales it to the window. */
class Stage : public juce::Component {
public:
    std::function<void(juce::Graphics&)> draw;
    void paint(juce::Graphics& g) override {
        if (draw) draw(g);
    }
};

/** The author's page: photo, a few words and where to follow him. */
class AboutPage : public juce::Component {
public:
    AboutPage();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override { setVisible(false); }
    std::function<void()> onClose;

private:
    juce::Image photo, signature;
    juce::OwnedArray<juce::TextButton> links;
    juce::TextButton close;
};

class AlptugDUBEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AlptugDUBEditor(AlptugDUBProcessor&);
    ~AlptugDUBEditor() override;
    void resized() override;

    static constexpr int W = 1100, H = 680;

private:
    void timerCallback() override;
    void layoutContent();
    void drawStage(juce::Graphics&);

    AlptugDUBProcessor& proc;
    ui::Look look;
    Stage content; // laid out at W x H, scaled to the window
    juce::Image wood, signature;

    Knob pitch, formant, range, growl, age, breath, nasal, body, air, mix, output;
    juce::ToggleButton follow, loudness;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAttach, loudnessAttach;
    juce::OwnedArray<juce::TextButton> characterButtons;
    juce::TextButton natural, wide, relearn, aboutButton;
    AboutPage about;

    float vuIn = 0, vuOut = 0, micGlow = 0;
    adub::VoiceSummary summary;
    float liveF0 = 0;
    juce::Rectangle<float> micArea, profileArea, vuInArea, vuOutArea, signatureArea;
};
