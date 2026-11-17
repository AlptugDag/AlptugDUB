// AlptugDUB by Alptuğ Dağ — Liquid Glass + Walnut Wood + Tactile Analog Console Editor.
#pragma once

#include "PluginProcessor.h"
#include "ui/Look.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

/** Knurled analog rotary knob with engraved caption above and Liquid Glass value pill below. */
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

/** Full-canvas custom painter scaled to the editor window. */
class Stage : public juce::Component {
public:
    std::function<void(juce::Graphics&)> draw;
    void paint(juce::Graphics& g) override {
        if (draw) draw(g);
    }
};

/** Hidden Liquid Glass Fine-Tune drawer for secondary texture knobs and DSP switches. */
class FineTuneDrawer : public juce::Component {
public:
    FineTuneDrawer(AlptugDUBProcessor& proc, const juce::String& closeText);
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;

private:
    Knob growl, breath, age, nasal, body, air, presence, output;
    juce::OwnedArray<juce::ToggleButton> switches;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> switchAttachments;
    juce::TextButton closeBtn;
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
    /** Advances meters, wave ribbons and pitch state by one frame (used by snapshot tool). */
    void refresh() { timerCallback(); }

    static constexpr int W = 1200, H = 720;

private:
    void timerCallback() override;
    void layoutContent();
    void drawStage(juce::Graphics&);
    void drawGlassWave(juce::Graphics&, juce::Rectangle<float> lens);
    void toggleFineTune();
    int currentCharacter() const;

    AlptugDUBProcessor& proc;
    ui::Look look;
    Stage content;
    juce::Image wood;
    std::unique_ptr<juce::Drawable> signature;

    // Main sparse studio controls (6 knobs + 5 tactile jewel-LED pushbuttons + 13 character keys)
    Knob regen, autotune;
    Knob pitch, formant;
    Knob glottal, mix;

    juce::ToggleButton aiBtn, guardBtn, linkBtn, autoMatchBtn, loudnessBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aiAttach, guardAttach, linkAttach, autoMatchAttach, loudnessAttach;

    juce::TextButton relearnBtn, fineTuneBtn, aboutBtn;
    juce::OwnedArray<juce::TextButton> characterButtons;

    FineTuneDrawer fineTuneDrawer;
    AboutPage about;

    float inLevel = 0, outLevel = 0, inPeak = 0, outPeak = 0, liveF0 = 0;
    float voicedLevel = 0, consonantLevel = 0, regenLevel = 0;
    float wavePhase = 0.0f, waveAmpSmooth = 0.0f;
    std::array<float, 64> waveRing{};
    adub::VoiceSummary summary;
    int detectedKey = 0, detectedScale = 2;
    float tuneCents = 0.0f;

    juce::Rectangle<float> headerArea, lensArea, leftModArea, centerModArea, rightModArea, charsArea;
};
