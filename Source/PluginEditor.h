#pragma once
#include "PluginProcessor.h"

// Custom look for the rotary knobs (arc style, dark theme)
struct AlphaLookAndFeel : public juce::LookAndFeel_V4
{
    AlphaLookAndFeel();

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;

    static juce::Colour accent() { return juce::Colour (0xffff9d3d); }
    static juce::Colour accent2() { return juce::Colour (0xff3fc8ff); }
};

class AlphaVerbEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit AlphaVerbEditor (AlphaVerbProcessor&);
    ~AlphaVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawDecayDisplay (juce::Graphics&, juce::Rectangle<float> area);

    struct Knob
    {
        juce::String paramId;
        juce::String name;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    AlphaVerbProcessor& processor;
    AlphaLookAndFeel laf;
    std::array<Knob, 7> knobs;
    juce::Rectangle<float> displayArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlphaVerbEditor)
};
