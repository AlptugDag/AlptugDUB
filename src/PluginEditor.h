// AlptugDUB by Alptuğ Dağ — the editor.
#pragma once

#include "PluginProcessor.h"
#include "ui/Look.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

/** A knob with a spaced caption above and its value below. */
class Knob : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& name, const juce::String& tip, bool big = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider slider;

private:
    juce::String title;
    bool large;
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

/** The author's page: photo, signature, a few words and where to follow him. */
class AboutPage : public juce::Component {
public:
    AboutPage();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override { setVisible(false); }

private:
    juce::Image photo;
    std::unique_ptr<juce::Drawable> signature;
    juce::OwnedArray<juce::TextButton> links;
    juce::TextButton close;
};

class AlptugDUBEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AlptugDUBEditor(AlptugDUBProcessor&);
    ~AlptugDUBEditor() override;
    void resized() override;
    /** Advances the meters and the pitch history by one frame (the snapshot tool drives it). */
    void refresh() { timerCallback(); }

    static constexpr int W = 1200, H = 720;

private:
    void timerCallback() override;
    void layoutContent();
    void drawStage(juce::Graphics&);
    void drawAnalysis(juce::Graphics&);
    int currentCharacter() const;

    AlptugDUBProcessor& proc;
    ui::Look look;
    Stage content; // laid out at W x H, scaled to the window
    juce::Image wood;
    std::unique_ptr<juce::Drawable> signature;

    Knob pitch, formant;
    Knob range, age, growl, breath, mix, output;
    juce::OwnedArray<juce::ToggleButton> switches; // the AI switches
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> switchAttachments;
    juce::TextButton natural, wide, relearn, aboutButton;
    juce::OwnedArray<juce::TextButton> characterButtons;
    AboutPage about;

    float inLevel = 0, outLevel = 0, inPeak = 0, outPeak = 0, liveF0 = 0;
    std::array<float, 180> history{}; // the last 6 seconds of pitch, Hz (0 = unvoiced)
    int historyPos = 0;
    adub::VoiceSummary summary;
    juce::Rectangle<float> panelArea, analysisArea, engineArea, metersArea, switchesArea, charactersArea;
    float confidence = 0;
};
