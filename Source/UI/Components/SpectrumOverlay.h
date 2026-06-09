#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Audio/SpectrumTap.h"

/** Translucent spectrum analyser overlay.
 *
 *  Reads from a SpectrumTap at 30 Hz and paints log-frequency magnitude
 *  bars with peak-hold + decay. Designed to overlay any panel; backdrop
 *  fades the existing UI so the spectrum stays readable.
 */
class SpectrumOverlay : public juce::Component,
                       private juce::Timer
{
public:
    explicit SpectrumOverlay (SpectrumTap& tap);

    void paint    (juce::Graphics&)      override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Mouse passes through to underlying components when not visible. */
    bool hitTest (int x, int y) override;

    void setVisible (bool shouldBeVisible) override;

    /** When the user clicks "close" we notify the host so it can untoggle
     *  any UI button. */
    std::function<void()> onCloseRequested;

private:
    void timerCallback() override;

    SpectrumTap&            tap;
    static constexpr int    kNumBins = 96;
    std::array<float, kNumBins> bins {};       // current magnitudes (dB)
    std::array<float, kNumBins> peaks {};      // peak-hold
    std::array<int,   kNumBins> peakAgeBlocks {};

    juce::Rectangle<int> closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumOverlay)
};
