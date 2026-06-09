#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/KnobComponent.h"

/** Right rail: TRANSPOSE + DOUBLER WIDTH + DOUBLER MIX, vertically stacked. */
class TopExtrasPanel : public juce::Component
{
public:
    TopExtrasPanel();

    void paint   (juce::Graphics&) override;
    void resized() override;
    void refreshFromEngine();

private:
    juce::Label   title;
    KnobComponent transpose;
    KnobComponent doublerWidth;
    KnobComponent doublerMix;
    KnobComponent output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopExtrasPanel)
};
