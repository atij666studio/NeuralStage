#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Horizontal Sweet-Spot / hit-zone meter.
 *  Three zone bands: TOO COLD (blue) | PERFECT (green) | TOO HOT (red),
 *  with a moving needle showing current input RMS in dB.
 */
class SweetSpotMeterBar : public juce::Component,
                          private juce::Timer
{
public:
    SweetSpotMeterBar();
    ~SweetSpotMeterBar() override = default;

    void paint   (juce::Graphics&) override;

private:
    void timerCallback() override;

    float rmsDbSmoothed { -60.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweetSpotMeterBar)
};
