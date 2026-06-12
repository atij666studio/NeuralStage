#include "SideRailPanel.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../Styles/AppLNF.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"
#include "../../Audio/FX/HitZoneDSP.h"

SideRailPanel::SideRailPanel()
{
    title.setText ("INPUT", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
    title.setJustificationType (juce::Justification::centred);
    title.setColour (juce::Label::textColourId, ns::Colours::textOnPanel);
    addAndMakeVisible (title);

    sweetSpot.setLabel ("SWEET SPOT").configure (0.0, 1.0, 0.5);
    autoLevel.setLabel ("AUTO LVL"  ).configure (0.0, 1.0, 0.5);
    sweetSpot.setKnobLNF (&ns::a2KnobLNF());
    autoLevel.setKnobLNF (&ns::a2KnobLNF());
    addAndMakeVisible (sweetSpot);
    addAndMakeVisible (autoLevel);

    auto& eng = App::get().getAudioEngine();
    sweetSpot.getSlider().setValue (eng.getSweetSpot().sweetSpot.load(),
                                    juce::dontSendNotification);
    sweetSpot.onValueChanged ([] (double v)
    {
        App::get().getAudioEngine().getSweetSpot().sweetSpot.store ((float) v);
    });
    autoLevel.onValueChanged ([] (double v)
    {
        App::get().getAudioEngine().setAutoLevelMacro ((float) v);
    });

    sweetSpot.bindMidi ("input.sweetSpot", "Sweet Spot");
    autoLevel.bindMidi ("input.autoLevel", "Auto Level");

    sweetSpot.setTooltipText ("Where in the dynamic sweet spot the gate / compressor sits. Higher = hotter into the amp.");
    autoLevel.setTooltipText ("Auto-leveller macro -- keeps the perceived loudness consistent across pickups and plugins.");

    // ON/OFF toggles for both processors. Both default to OFF so the
    // dry DI runs untouched until the user opts in. The button text +
    // colour reflect state, and a right-click learns MIDI like other
    // controls in the app.
    auto styleToggle = [] (juce::TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setConnectedEdges (0);
        b.setColour (juce::TextButton::buttonColourId,
                     ns::Colours::panel.withAlpha (0.4f));
        b.setColour (juce::TextButton::buttonOnColourId,
                     ns::Colours::tealAccent.withAlpha (0.85f));
        b.setColour (juce::TextButton::textColourOffId, ns::Colours::textOnPanelDim);
        b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    };
    styleToggle (sweetSpotOn);
    styleToggle (autoLevelOn);

    sweetSpotOn.setToggleState (! eng.getSweetSpot().bypassed.load(),
                                juce::dontSendNotification);
    sweetSpotOn.setButtonText (sweetSpotOn.getToggleState() ? "ON" : "OFF");
    sweetSpotOn.onClick = [this]
    {
        const bool on = sweetSpotOn.getToggleState();
        App::get().getAudioEngine().getSweetSpot().bypassed.store (! on);
        sweetSpotOn.setButtonText (on ? "ON" : "OFF");
    };

    autoLevelOn.setToggleState (eng.isAutoLevelOn(), juce::dontSendNotification);
    autoLevelOn.setButtonText (autoLevelOn.getToggleState() ? "ON" : "OFF");
    autoLevelOn.onClick = [this]
    {
        const bool on = autoLevelOn.getToggleState();
        App::get().getAudioEngine().setAutoLevelOn (on);
        autoLevelOn.setButtonText (on ? "ON" : "OFF");
    };

    addAndMakeVisible (sweetSpotOn);
    addAndMakeVisible (autoLevelOn);

    statusLabel.setText ("--", juce::dontSendNotification);
    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, ns::Colours::textOnPanel);
    addAndMakeVisible (statusLabel);

    grLabel.setText ("0.0 dB", juce::dontSendNotification);
    grLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    grLabel.setJustificationType (juce::Justification::centred);
    grLabel.setColour (juce::Label::textColourId, ns::Colours::textOnPanelDim);
    addAndMakeVisible (grLabel);

    startTimerHz (15);
}

void SideRailPanel::timerCallback()
{
    auto& eng = App::get().getAudioEngine();
    const int s = eng.getSweetSpot().statusEnum.load();
    const char* txt = "PERFECT";
    juce::Colour col = ns::Colours::tealAccent;
    if (s == 0) { txt = "TOO COLD"; col = juce::Colour (0xFF4A6CC9); }
    else if (s == 2) { txt = "TOO HOT"; col = ns::Colours::red; }
    statusLabel.setText (txt, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, col);

    grLabel.setText (juce::String (eng.getAutoLevel().gainDb.load(), 1) + " dB",
                     juce::dontSendNotification);
}

void SideRailPanel::paint (juce::Graphics& g)
{
    g.setColour (ns::Colours::lavender);
    g.fillRoundedRectangle (getLocalBounds().toFloat(),
                            (float) ns::UI::kPanelRadius);
}

void SideRailPanel::resized()
{
    using namespace ns::UI;
    auto r = getLocalBounds().reduced (10, 12);

    title.setBounds (r.removeFromTop (kRailTitleH));
    r.removeFromTop (4);

    // Reserve the bottom for the tuner (placed by MainComponent).
    const int reserveBottom = kTunerH + 6;
    r.removeFromBottom (reserveBottom);

    // Two knob cells stacked vertically. Preferred size is kRailKnobCellH+18;
    // when the rail is shorter (e.g. Beelink 1366x768 full screen), divide
    // the available space evenly so both cells always stay fully visible.
    // Minimum 80 px per cell keeps the knob + label + toggle legible.
    const int gapBetween = 4;
    const int available  = r.getHeight();
    const int cellH = juce::jmax (80, (available - gapBetween) / 2);

    auto cell1 = r.removeFromTop (cellH);
    {
        auto toggleRow = cell1.removeFromBottom (16);
        auto labelRow  = cell1.removeFromBottom (16);
        sweetSpot   .setBounds (cell1);
        statusLabel .setBounds (labelRow);
        sweetSpotOn .setBounds (toggleRow.withSizeKeepingCentre (60, 16));
    }

    r.removeFromTop (gapBetween);

    auto cell2 = r.removeFromTop (cellH);
    {
        auto toggleRow = cell2.removeFromBottom (16);
        auto labelRow  = cell2.removeFromBottom (16);
        autoLevel   .setBounds (cell2);
        grLabel     .setBounds (labelRow);
        autoLevelOn .setBounds (toggleRow.withSizeKeepingCentre (60, 16));
    }
}

void SideRailPanel::refreshFromEngine()
{
    auto& eng = App::get().getAudioEngine();
    sweetSpot.getSlider().setValue (eng.getSweetSpot().sweetSpot.load(),
                                    juce::dontSendNotification);
    autoLevel.getSlider().setValue (eng.getAutoLevelMacro(),
                                    juce::dontSendNotification);
    sweetSpotOn.setToggleState (! eng.getSweetSpot().bypassed.load(),
                                juce::dontSendNotification);
    sweetSpotOn.setButtonText (sweetSpotOn.getToggleState() ? "ON" : "OFF");
    autoLevelOn.setToggleState (eng.isAutoLevelOn(), juce::dontSendNotification);
    autoLevelOn.setButtonText (autoLevelOn.getToggleState() ? "ON" : "OFF");
}
