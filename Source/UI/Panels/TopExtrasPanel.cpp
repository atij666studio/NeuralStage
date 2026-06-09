#include "TopExtrasPanel.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../Styles/AppLNF.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"

TopExtrasPanel::TopExtrasPanel()
{
    title.setText ("PITCH / DOUBLER", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
    title.setJustificationType (juce::Justification::centred);
    title.setColour (juce::Label::textColourId, ns::Colours::textOnPanel);
    addAndMakeVisible (title);

    transpose.setLabel ("TRANSPOSE").configure (-12.0, 12.0, 0.0, "st");
    transpose.setKnobLNF (&ns::a2KnobLNF());
    transpose.onValueChanged ([] (double v)
    {
        App::get().getAudioEngine().getTranspose().setSemitones ((float) v);
    });
    addAndMakeVisible (transpose);

    doublerWidth.setLabel ("WIDTH").configure (0.0, 1.0, 0.0);
    doublerWidth.setKnobLNF (&ns::a2KnobLNF());
    doublerWidth.onValueChanged ([] (double v)
    {
        App::get().getAudioEngine().setDoublerWidth ((float) v);
    });
    addAndMakeVisible (doublerWidth);

    doublerMix.setLabel ("DOUBLER MIX").configure (0.0, 1.0, 0.0);
    doublerMix.setKnobLNF (&ns::a2KnobLNF());
    doublerMix.onValueChanged ([] (double v)
    {
        App::get().getAudioEngine().setDoublerMix ((float) v);
    });
    addAndMakeVisible (doublerMix);

    output.setLabel ("OUTPUT").configure (-24.0, 24.0, 0.0, " dB");
    output.setKnobLNF (&ns::a2KnobLNF());
    output.onValueChanged ([] (double v)
    {
        App::get().getAudioEngine().getOutput().setPostGainDb ((float) v);
    });
    addAndMakeVisible (output);

    transpose   .bindMidi ("pitch.transpose",    "Transpose");
    doublerWidth.bindMidi ("doubler.width",      "Doubler Width");
    doublerMix  .bindMidi ("doubler.mix",        "Doubler Mix");
    output      .bindMidi ("output.gain",        "Output Gain");

    transpose   .setTooltipText ("Pitch-shift the input +/-12 semitones (real-time, granular).");
    doublerWidth.setTooltipText ("Stereo spread of the doubler image (0 = mono, 1 = wide).");
    doublerMix  .setTooltipText ("Doubler wet/dry blend.");
    output      .setTooltipText ("Master output gain (post-FX, pre-safety-limiter).");
}

void TopExtrasPanel::paint (juce::Graphics& g)
{
    g.setColour (ns::Colours::lavender);
    g.fillRoundedRectangle (getLocalBounds().toFloat(),
                            (float) ns::UI::kPanelRadius);
}

void TopExtrasPanel::resized()
{
    using namespace ns::UI;
    auto r = getLocalBounds().reduced (10, 12);
    title.setBounds (r.removeFromTop (kRailTitleH));
    r.removeFromTop (4);

    // Divide the remaining area evenly between the four knob cells so all
    // four scale together when the window is resized -- never let OUTPUT
    // (the bottom cell) get squeezed to a sliver while the upper three keep
    // their fixed kRailKnobCellH size.
    const int cellH = juce::jmax (60, r.getHeight() / 4);
    transpose   .setBounds (r.removeFromTop (cellH));
    doublerWidth.setBounds (r.removeFromTop (cellH));
    doublerMix  .setBounds (r.removeFromTop (cellH));
    output      .setBounds (r);
}

void TopExtrasPanel::refreshFromEngine()
{
    auto& eng = App::get().getAudioEngine();
    transpose   .getSlider().setValue (eng.getTranspose().getSemitones(), juce::dontSendNotification);
    doublerWidth.getSlider().setValue (eng.getDoublerWidth(),             juce::dontSendNotification);
    doublerMix  .getSlider().setValue (eng.getDoublerMix(),               juce::dontSendNotification);
    output      .getSlider().setValue (eng.getOutput().getPostGainDb(),   juce::dontSendNotification);
}
