#pragma once
#include <juce_graphics/juce_graphics.h>

namespace ns::Colours
{
    // App frame
    inline const juce::Colour background    { 0xFF0E0E12 };

    // Lavender NAM-A2 panel surfaces
    inline const juce::Colour lavender      { 0xFFC5B6E0 };
    inline const juce::Colour lavenderDeep  { 0xFFB0A0CD };
    inline const juce::Colour panel         { 0xFFC5B6E0 };
    inline const juce::Colour panelLight    { 0xFFD4C8E8 };

    // Teal accents (selected segments / active highlights). tealLight /
    // cyanDrop are kept as their own tokens because they are independent
    // brightness ramps off the main accent.
    inline const juce::Colour tealLight     { 0xFF66A8B0 };
    inline const juce::Colour cyanDrop      { 0xFF88BCC9 };

    // Chip colours (segmented buttons). chipUnsel is intentionally a touch
    // darker than ground steel for the premium-hardware look against the
    // lavender panels (was 0xFF7B8A93 -- felt washed out at small sizes).
    inline const juce::Colour chipUnsel     { 0xFF5A6770 };
    inline const juce::Colour chipSel       { 0xFF101820 };

    // Accent. tealAccent is an alias kept for legacy call sites; both point
    // at a single source-of-truth value so changes propagate everywhere.
    inline const juce::Colour accent        { 0xFF2A6F75 };
    inline const juce::Colour tealAccent    = accent;
    inline const juce::Colour accentHover   { 0xFF3A8993 };
    inline const juce::Colour accentGlow    { 0xFF66A8B0 };

    // Text
    inline const juce::Colour textPrimary   { 0xFFEAEAF0 };
    inline const juce::Colour textSecondary { 0xFFA0A0B0 };
    inline const juce::Colour textDisabled  { 0xFF5A5A6A };
    inline const juce::Colour textOnPanel   { 0xFF0A0A12 };
    inline const juce::Colour textOnPanelDim{ 0xFF38384A };

    // Status
    inline const juce::Colour green         { 0xFF6FE39A };
    inline const juce::Colour yellow        { 0xFFFFD54F };
    inline const juce::Colour red           { 0xFFFF5A5A };
}
