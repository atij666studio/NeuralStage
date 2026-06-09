#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/BlendPad.h"

/** Centre LCD: blend pad with 4 NAM model loaders surrounding it.
 *  A = top (horizontal), B = bottom (horizontal),
 *  C = left (vertical),  D = right (vertical).
 */
class NamLcdPanel : public juce::Component,
                    private juce::Timer
{
public:
    NamLcdPanel();
    ~NamLcdPanel() override;

    void paint   (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Pull blend-pad position back from engine NAM weights (used by undo/redo). */
    void refreshFromEngine();

private:
    void timerCallback() override;
    void showSlotMenu        (int slot);
    void showPanelMenu       ();
    void chooseAndLoadNamFile (int slot);
    void loadFile             (int slot, const juce::File& f);
    void refreshSlotLabels();

    BlendPad         blendPad;
    juce::TextButton slotButtons[4]
    {
        juce::TextButton ("A: empty"),
        juce::TextButton ("B: empty"),
        juce::TextButton ("C: empty"),
        juce::TextButton ("D: empty")
    };
    // Per-slot slim-size sliders. Only visible when the loaded model is a
    // SlimmableContainer (NAM A2). Range 0..1, default 1.0 (full quality).
    juce::Slider     slimSliders[4];
    juce::Label statusLabel;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamLcdPanel)
};
