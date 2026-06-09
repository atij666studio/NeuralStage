#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Translucent banner overlaid in the LCD area while plugin scanning is active.
 *  Polls PluginManager via MainComponent's timer.
 */
class ScanStatusOverlay : public juce::Component
{
public:
    ScanStatusOverlay();

    /** Refresh from the PluginManager. Returns true if scanning is active. */
    bool refresh();

    void paint   (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextButton cancelBtn { "Cancel" };
    juce::String currentText;
    float        progress { 0.0f };
    bool         active   { false };
};
