#include "SweetSpotMeter.h"
#include "../Styles/Colours.h"

SweetSpotMeter::SweetSpotMeter() = default;

void SweetSpotMeter::setLevel (float normalised01)
{
    const auto v = juce::jlimit (0.0f, 1.0f, normalised01);
    if (v != level)
    {
        level = v;
        repaint();
    }
}

void SweetSpotMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float radius = area.getHeight() * 0.5f;

    // Background
    g.setColour (ns::Colours::panelLight);
    g.fillRoundedRectangle (area, radius);

    const float w = area.getWidth();
    auto bar = area;

    g.setColour (ns::Colours::green);
    g.fillRoundedRectangle (bar.removeFromLeft (w * 0.60f), radius);

    g.setColour (ns::Colours::yellow);
    g.fillRect (bar.removeFromLeft (w * 0.25f));

    g.setColour (ns::Colours::red);
    g.fillRect (bar);

    // Indicator
    const float x = juce::jlimit (0.0f, w, w * level);
    g.setColour (ns::Colours::textPrimary.withAlpha (0.85f));
    g.fillRoundedRectangle (juce::Rectangle<float> (x - 1.5f, area.getY() - 2.0f,
                                                    3.0f, area.getHeight() + 4.0f),
                            1.5f);
}
