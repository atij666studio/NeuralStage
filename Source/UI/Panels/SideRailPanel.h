#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/KnobComponent.h"

/** Left rail: SWEET SPOT + AUTO LEVEL knobs with status readouts. */
class SideRailPanel : public juce::Component,
                      private juce::Timer
{
public:
    SideRailPanel();
    ~SideRailPanel() override = default;

    void paint   (juce::Graphics&) override;
    void resized() override;
    void refreshFromEngine();

    /** Y of the bottom of the AUTO LVL knob's "X.X dB" label, in this
     *  panel's local coordinates. Used by MainComponent to anchor the
     *  floating blue dB readout strip just below the side-rail knobs. */
    int  getAutoLevelBottom() const { return grLabel.getBottom(); }

private:
    void timerCallback() override;

    juce::Label    title;
    KnobComponent  sweetSpot;
    juce::TextButton sweetSpotOn { "OFF" };
    juce::Label    statusLabel;
    KnobComponent  autoLevel;
    juce::TextButton autoLevelOn { "OFF" };
    juce::Label    grLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideRailPanel)
};
