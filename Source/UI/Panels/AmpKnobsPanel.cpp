#include "AmpKnobsPanel.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../Styles/AppLNF.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"

AmpKnobsPanel::AmpKnobsPanel()
{
    auto& eng = App::get().getAudioEngine();

    input    .setLabel ("INPUT")   .configure (-24.0, 24.0, 0.0, "dB");
    gain     .setLabel ("GAIN")    .configure (-24.0, 24.0, 0.0, "dB");
    bass     .setLabel ("BASS")    .configure (-12.0, 12.0, 0.0, "dB");
    mid      .setLabel ("MID")     .configure (-12.0, 12.0, 0.0, "dB");
    treble   .setLabel ("TREBLE")  .configure (-12.0, 12.0, 0.0, "dB");
    presence .setLabel ("PRES")    .configure (-12.0, 12.0, 0.0, "dB");
    tight    .setLabel ("TIGHT")   .configure (0.0, 1.0, 0.0);
    body     .setLabel ("BODY")    .configure (0.0, 1.0, 0.5);
    air      .setLabel ("AIR")     .configure (0.0, 1.0, 0.5);

    KnobComponent* all[] { &input, &gain, &bass, &mid, &treble, &presence,
                           &tight, &body, &air };
    for (auto* k : all)
    {
        k->setKnobLNF (&ns::a2KnobLNF());
        addAndMakeVisible (*k);
    }

    input   .onValueChanged ([&eng] (double v) { eng.getInput() .setPreGainDb  ((float) v); });
    gain    .onValueChanged ([&eng] (double v) { eng.getNAM()   .setPreGain    ((float) v); });
    bass    .onValueChanged ([&eng] (double v) { eng.getEQ()    .setBass       ((float) v); });
    mid     .onValueChanged ([&eng] (double v) { eng.getEQ()    .setMid        ((float) v); });
    treble  .onValueChanged ([&eng] (double v) { eng.getEQ()    .setTreble     ((float) v); });
    presence.onValueChanged ([&eng] (double v) { eng.getOutput().setPostGainDb ((float) v); });
    tight   .onValueChanged ([&eng] (double v) { eng.setTight ((float) v); });
    body    .onValueChanged ([&eng] (double v) { eng.setBody  ((float) v); });
    air     .onValueChanged ([&eng] (double v) { eng.setAir   ((float) v); });

    // Seed knob defaults from current engine state so the UI matches what's
    // actually live on launch.
    tight   .getSlider().setValue (eng.getTight(), juce::dontSendNotification);
    body    .getSlider().setValue (eng.getBody(),  juce::dontSendNotification);
    air     .getSlider().setValue (eng.getAir(),   juce::dontSendNotification);

    // Make every amp knob right-clickable for MIDI Learn.
    input   .bindMidi ("amp.input",    "Input Gain");
    gain    .bindMidi ("amp.gain",     "NAM Drive");
    bass    .bindMidi ("amp.bass",     "EQ Bass");
    mid     .bindMidi ("amp.mid",      "EQ Mid");
    treble  .bindMidi ("amp.treble",   "EQ Treble");
    presence.bindMidi ("amp.presence", "Output Level");
    tight   .bindMidi ("amp.tight",    "Tight");
    body    .bindMidi ("amp.body",     "Body");
    air     .bindMidi ("amp.air",      "Air");

    // Hover tooltips. Right-click any knob for Reset / Type / Copy / Paste / MIDI Learn.
    input   .setTooltipText ("Input gain trim into the rig (+/-24 dB).");
    gain    .setTooltipText ("Pre-gain into the NAM model -- amp drive amount.");
    bass    .setTooltipText ("Low-shelf EQ before the cab (+/-12 dB).");
    mid     .setTooltipText ("Mid-band EQ before the cab (+/-12 dB).");
    treble  .setTooltipText ("Treble EQ before the cab (+/-12 dB).");
    presence.setTooltipText ("Master output level after the cab (+/-12 dB).");
    tight   .setTooltipText ("High-pass tightener before the amp -- cleans up the low end.");
    body    .setTooltipText ("Low-mid character around 250 Hz.");
    air     .setTooltipText ("Top-end shimmer above 8 kHz.");

    // MUTE INPUT button: visible cousin of the red MUTE INPUT button in the
    // Audio/MIDI Setup dialog. App::initialise() mutes the input on launch
    // as a feedback-safety measure -- without this main-UI surface the user
    // had no way to know about it. Toggles eng.getInput().setMute(). A 10 Hz
    // poll keeps the button in sync if the mute is changed from elsewhere
    // (Setup dialog, Tuner panel auto-mute, MIDI learn, etc.).
    inputMute.setClickingTogglesState (true);
    inputMute.setButtonText ("MUTE");
    inputMute.setTooltip ("Mute input signal. ON at launch as a feedback-safety measure -- click to unmute when you're ready to play.");
    inputMute.setColour (juce::TextButton::buttonColourId,
                         ns::Colours::panel.withAlpha (0.45f));
    inputMute.setColour (juce::TextButton::buttonOnColourId,
                         juce::Colour (0xffe53935)); // red when muted
    inputMute.setColour (juce::TextButton::textColourOffId, ns::Colours::textOnPanelDim);
    inputMute.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    inputMute.setToggleState (eng.getInput().isMuted(), juce::dontSendNotification);
    inputMute.onClick = [this]
    {
        App::get().getAudioEngine().getInput().setMute (inputMute.getToggleState());
    };
    addAndMakeVisible (inputMute);

    startTimerHz (10);
}

void AmpKnobsPanel::paint (juce::Graphics& g)
{
    g.setColour (ns::Colours::lavender);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) ns::UI::kPanelRadius);
}

void AmpKnobsPanel::resized()
{
    auto r = getLocalBounds().reduced (20, 12);
    KnobComponent* all[] { &input, &gain, &bass, &mid, &treble, &presence,
                           &tight, &body, &air };
    constexpr int n = 9;
    constexpr int gap = 6;
    const int cellW = (r.getWidth() - (n - 1) * gap) / n;
    int x = r.getX();
    for (int i = 0; i < n; ++i)
    {
        all[i]->setBounds (x, r.getY(), cellW, r.getHeight());
        x += cellW + gap;
    }

    // Overlay the MUTE button on the top-right corner of the INPUT knob's
    // cell -- compact, doesn't push the row layout around.
    auto inBounds = input.getBounds();
    const int mw = juce::jmin (44, inBounds.getWidth() - 6);
    const int mh = 16;
    inputMute.setBounds (inBounds.getRight() - mw - 2,
                         inBounds.getY() + 2,
                         mw, mh);
}

void AmpKnobsPanel::timerCallback()
{
    // Keep the MUTE button in sync with engine state -- it can be flipped
    // from the Audio/MIDI Setup dialog, the Tuner panel auto-mute, or via
    // MIDI learn. Cheap atomic read; no callback fired.
    const bool muted = App::get().getAudioEngine().getInput().isMuted();
    if (inputMute.getToggleState() != muted)
        inputMute.setToggleState (muted, juce::dontSendNotification);
}

void AmpKnobsPanel::refreshFromEngine()
{
    auto& eng = App::get().getAudioEngine();
    input   .getSlider().setValue (eng.getInput() .getPreGainDb (),   juce::dontSendNotification);
    gain    .getSlider().setValue (eng.getNAM()   .getPreGain   (),   juce::dontSendNotification);
    bass    .getSlider().setValue (eng.getEQ()    .getBass      (),   juce::dontSendNotification);
    mid     .getSlider().setValue (eng.getEQ()    .getMid       (),   juce::dontSendNotification);
    treble  .getSlider().setValue (eng.getEQ()    .getTreble    (),   juce::dontSendNotification);
    presence.getSlider().setValue (eng.getOutput().getPostGainDb(),   juce::dontSendNotification);
    tight   .getSlider().setValue (eng.getTight(),                    juce::dontSendNotification);
    body    .getSlider().setValue (eng.getBody (),                    juce::dontSendNotification);
    air     .getSlider().setValue (eng.getAir  (),                    juce::dontSendNotification);
}
