#include "PluginEditor.h"
#include <cmath>

//==============================================================================
AlphaLookAndFeel::AlphaLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffd5dbe8));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent().withAlpha (0.4f));
}

void AlphaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // knob body
    juce::ColourGradient body (juce::Colour (0xff2a3142), centre.x, centre.y - radius,
                               juce::Colour (0xff12161f), centre.x, centre.y + radius, false);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - radius * 0.72f, centre.y - radius * 0.72f, radius * 1.44f, radius * 1.44f);
    g.setColour (juce::Colour (0xff3a4358));
    g.drawEllipse (centre.x - radius * 0.72f, centre.y - radius * 0.72f, radius * 1.44f, radius * 1.44f, 1.0f);

    // track
    const float arcR = radius - 3.0f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff262d3d));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (accent());
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -radius * 0.68f, 3.0f, radius * 0.34f, 1.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (juce::Colours::white);
    g.fillPath (pointer);
}

//==============================================================================
AlphaVerbEditor::AlphaVerbEditor (AlphaVerbProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);
    setSize (780, 470);

    const std::array<std::pair<const char*, const char*>, 7> defs {{
        { "mix", "MIX" },
        { "decay", "DECAY" },
        { "size", "SPACE" },
        { "predelay", "PREDELAY" },
        { "brightness", "BRIGHTNESS" },
        { "lowdecay", "LOW DECAY" },
        { "width", "WIDTH" }
    }};

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.paramId = defs[i].first;
        k.name = defs[i].second;

        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, k.paramId, k.slider);
    }

    startTimerHz (30);
}

AlphaVerbEditor::~AlphaVerbEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AlphaVerbEditor::timerCallback()
{
    repaint (displayArea.toNearestInt());
}

void AlphaVerbEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (44);                         // header
    displayArea = area.removeFromTop (250).toFloat();
    area.removeFromTop (28);                         // knob labels row

    const int w = area.getWidth() / (int) knobs.size();
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i].slider.setBounds (area.getX() + (int) i * w, area.getY(), w, area.getHeight());
}

//==============================================================================
void AlphaVerbEditor::paint (juce::Graphics& g)
{
    // background
    juce::ColourGradient bg (juce::Colour (0xff0b0e15), 0.0f, 0.0f,
                             juce::Colour (0xff171d2b), 0.0f, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // header
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("ALPHAVERB", 20, 12, 300, 28, juce::Justification::centredLeft);
    g.setColour (AlphaLookAndFeel::accent());
    g.fillRoundedRectangle (20.0f, 42.0f, 46.0f, 3.0f, 1.5f);

    g.setColour (juce::Colour (0xff7d869c));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("REVERB", getWidth() - 220, 14, 200, 24, juce::Justification::centredRight);

    drawDecayDisplay (g, displayArea);

    // knob labels
    g.setColour (juce::Colour (0xff9aa3b8));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    for (auto& k : knobs)
    {
        auto b = k.slider.getBounds();
        g.drawText (k.name, b.getX(), b.getY() - 22, b.getWidth(), 18, juce::Justification::centred);
    }
}

void AlphaVerbEditor::drawDecayDisplay (juce::Graphics& g, juce::Rectangle<float> r)
{
    // frame
    g.setColour (juce::Colour (0xff0f131c));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (0xff2a3245));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    auto plot = r.reduced (16.0f, 14.0f);
    plot.removeFromLeft (26.0f);
    plot.removeFromBottom (14.0f);

    constexpr float fMin = 20.0f, fMax = 20000.0f;
    constexpr float tMin = 0.1f, tMax = 30.0f;

    auto xForFreq = [&] (float f)
    {
        return plot.getX() + plot.getWidth() * std::log (f / fMin) / std::log (fMax / fMin);
    };
    auto yForTime = [&] (float t)
    {
        const float n = std::log (juce::jlimit (tMin, tMax, t) / tMin) / std::log (tMax / tMin);
        return plot.getBottom() - plot.getHeight() * n;
    };

    // grid
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (juce::Colour (0xff1d2434));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        if (f == 100.0f || f == 1000.0f || f == 10000.0f)
        {
            g.setColour (juce::Colour (0xff6c758b));
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                        (int) x - 20, (int) plot.getBottom() + 1, 40, 12, juce::Justification::centred);
        }
    }
    for (float t : { 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f })
    {
        const float y = yForTime (t);
        g.setColour (juce::Colour (0xff1d2434));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (juce::Colour (0xff6c758b));
        g.drawText (juce::String (t, t < 1.0f ? 1 : 0) + "s", (int) r.getX() + 6, (int) y - 6, 30, 12,
                    juce::Justification::centredLeft);
    }

    // parameters
    const float decay      = processor.apvts.getRawParameterValue ("decay")->load();
    const float brightness = processor.apvts.getRawParameterValue ("brightness")->load();
    const float lowDecay   = processor.apvts.getRawParameterValue ("lowdecay")->load();
    const float cutoff     = juce::jmap (brightness, 1500.0f, 18000.0f);

    // decay-time-vs-frequency curve
    juce::Path curve, fill;
    constexpr int numPoints = 220;
    for (int i = 0; i <= numPoints; ++i)
    {
        const float f = fMin * std::pow (fMax / fMin, (float) i / (float) numPoints);
        const float lowW = 1.0f / (1.0f + std::pow (f / 300.0f, 2.0f));
        const float lowFactor = 1.0f + (lowDecay - 1.0f) * lowW;
        const float hf = 1.0f / (1.0f + std::pow (f / cutoff, 3.0f));
        const float t = decay * lowFactor * (0.08f + 0.92f * hf);

        const float x = xForFreq (f);
        const float y = yForTime (t);
        if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, plot.getBottom()); fill.lineTo (x, y); }
        else        { curve.lineTo (x, y); fill.lineTo (x, y); }
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    juce::ColourGradient fillGrad (AlphaLookAndFeel::accent().withAlpha (0.35f), 0.0f, plot.getY(),
                                   AlphaLookAndFeel::accent().withAlpha (0.02f), 0.0f, plot.getBottom(), false);
    g.setGradientFill (fillGrad);
    g.fillPath (fill);

    g.setColour (AlphaLookAndFeel::accent());
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // big readout
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.drawText (juce::String (decay, 2) + " s", (int) r.getRight() - 170, (int) r.getY() + 12, 150, 36,
                juce::Justification::centredRight);
    g.setColour (AlphaLookAndFeel::accent2());
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("DECAY TIME", (int) r.getRight() - 170, (int) r.getY() + 46, 150, 14,
                juce::Justification::centredRight);
}
