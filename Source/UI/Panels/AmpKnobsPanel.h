#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/KnobComponent.h"

/** Top strip: 9 amp/EQ knobs in a single row, NAM-A2 lavender panel. */
class AmpKnobsPanel : public juce::Component,
                      private juce::Timer
{
public:
    AmpKnobsPanel();
    ~AmpKnobsPanel() override = default;

    void paint   (juce::Graphics&) override;
    void resized() override;

    /** Pull current engine values into every knob (no callback fired).
     *  Call after Undo/Redo, A/B switch, scene recall or preset load. */
    void refreshFromEngine();

private:
    void timerCallback() override;

    KnobComponent input, gain, bass, mid, treble, presence;
    KnobComponent tight, body, air;

    // Visible MUTE INPUT button that overlays the top-right corner of the
    // INPUT knob's cell, mirroring the red MUTE INPUT control in the Audio/
    // MIDI Setup dialog so the user can mute / unmute from the main UI.
    juce::TextButton inputMute;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpKnobsPanel)
};
