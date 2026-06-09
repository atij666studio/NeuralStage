#include "CabPanel.h"
#include "../Styles/Colours.h"

CabPanel::CabPanel()
{
    micType.addItemList ({ "SM57", "MD421", "R121", "U87" }, 1);
    micType.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (micType);

    distance.addItemList ({ "On-axis", "Edge", "Cap", "1\"", "3\"", "12\"" }, 1);
    distance.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (distance);

    addAndMakeVisible (ir1Btn);
    addAndMakeVisible (ir2Btn);

    blend.setSliderStyle (juce::Slider::LinearHorizontal);
    blend.setRange (0.0, 1.0, 0.0);
    blend.setValue (0.5);
    blend.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (blend);
}

void CabPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ns::Colours::panel);
    g.fillRoundedRectangle (b, 20.0f);

    // Speaker disc placeholder
    auto speaker = b.reduced (40.0f).removeFromTop (b.getHeight() * 0.55f);
    const float d = std::min (speaker.getWidth(), speaker.getHeight());
    auto disc = juce::Rectangle<float> (d, d).withCentre (speaker.getCentre());
    g.setColour (ns::Colours::panelLight);
    g.fillEllipse (disc);
    g.setColour (ns::Colours::accent.withAlpha (0.4f));
    g.drawEllipse (disc, 2.0f);

    // Mic dot
    const float dotR = 8.0f;
    const auto dotCentre = disc.getCentre()
        + juce::Point<float> ((micPos.x - 0.5f) * disc.getWidth() * 0.8f,
                              (micPos.y - 0.5f) * disc.getHeight() * 0.8f);
    g.setColour (ns::Colours::accentGlow);
    g.fillEllipse (juce::Rectangle<float> (dotR * 2, dotR * 2).withCentre (dotCentre));
}

void CabPanel::resized()
{
    auto b = getLocalBounds().reduced (24);
    auto bottom = b.removeFromBottom (110);

    auto row1 = bottom.removeFromTop (32);
    micType .setBounds (row1.removeFromLeft (180));
    row1.removeFromLeft (16);
    distance.setBounds (row1.removeFromLeft (180));

    bottom.removeFromTop (12);
    auto row2 = bottom.removeFromTop (40);
    ir1Btn.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (12);
    ir2Btn.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (16);
    blend.setBounds  (row2);
}
