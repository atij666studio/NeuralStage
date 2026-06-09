#pragma once
#include <juce_graphics/juce_graphics.h>

namespace ns::Fonts
{
    inline juce::Font preset()       { return juce::Font (28.0f, juce::Font::bold); }
    inline juce::Font sectionLabel() { return juce::Font (14.0f, juce::Font::plain).withExtraKerningFactor (0.10f); }
    inline juce::Font knobLabel()    { return juce::Font (12.0f, juce::Font::plain).withExtraKerningFactor (0.08f); }
    inline juce::Font value()        { return juce::Font (16.0f, juce::Font::bold); }
    inline juce::Font small()        { return juce::Font (12.0f, juce::Font::plain); }
}
