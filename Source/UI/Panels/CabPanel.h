#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class CabPanel : public juce::Component
{
public:
    CabPanel();
    ~CabPanel() override = default;

    void paint   (juce::Graphics&) override;
    void resized() override;

private:
    juce::Point<float> micPos { 0.5f, 0.5f }; // normalised on speaker
    juce::ComboBox micType, distance;
    juce::TextButton ir1Btn { "IR 1" }, ir2Btn { "IR 2" };
    juce::Slider blend;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabPanel)
};
