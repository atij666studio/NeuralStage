#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class SweetSpotMeter : public juce::Component
{
public:
    SweetSpotMeter();
    ~SweetSpotMeter() override = default;

    void paint (juce::Graphics&) override;

    void setLevel (float normalised01);

private:
    float level { 0.5f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweetSpotMeter)
};
