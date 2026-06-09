#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class KnobComponent : public juce::Component,
                      private juce::Slider::Listener
{
public:
    KnobComponent();
    ~KnobComponent() override;

    void paint   (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    KnobComponent& setLabel (const juce::String& text);
    KnobComponent& configure (double min, double max, double defaultValue,
                              const juce::String& suffix = {});

    /** Sets the same tooltip on the slider, name and value labels so it
     *  appears no matter where the user hovers within the knob cell. */
    KnobComponent& setTooltipText (const juce::String& text);

    /** Register this knob with the global MIDI-learn registry. After calling
     *  this, right-clicking the knob shows a Learn / Clear menu and any
     *  learned CC/Note will set the slider 0..1 across its full range. */
    void bindMidi (const juce::String& paramId, const juce::String& displayName);

    juce::Slider& getSlider() noexcept { return slider; }
    void setKnobLNF (juce::LookAndFeel* lnf) { slider.setLookAndFeel (lnf); }

    void onValueChanged (std::function<void (double)> cb)
    {
        externalCb = std::move (cb);
    }

private:
    void sliderValueChanged  (juce::Slider*) override;
    void sliderDragStarted   (juce::Slider*) override;
    void updateValueText();
    void showContextMenu();
    void promptForValue();

    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    juce::String suffix;
    int          decimals { 1 };

    double       defaultValue { 0.0 };
    bool         hasDefault   { false };

    juce::String midiParamId;
    juce::String midiDisplayName;

    std::function<void (double)> externalCb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};
