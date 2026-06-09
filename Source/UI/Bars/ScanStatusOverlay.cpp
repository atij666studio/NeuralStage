#include "ScanStatusOverlay.h"
#include "../Styles/Colours.h"
#include "../Styles/Fonts.h"
#include "../../App.h"
#include "../../PluginHost/PluginManager.h"

ScanStatusOverlay::ScanStatusOverlay()
{
    // The overlay backdrop ignores mouse input but its children (Cancel) do not.
    setInterceptsMouseClicks (false, true);

    cancelBtn.setColour (juce::TextButton::buttonColourId,  ns::Colours::chipUnsel);
    cancelBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    cancelBtn.onClick = []
    {
        App::get().getPluginManager().cancelAsyncScan();
    };
    addAndMakeVisible (cancelBtn);
}

bool ScanStatusOverlay::refresh()
{
    auto& mgr = App::get().getPluginManager();
    const bool scanning = mgr.isScanning();
    if (scanning != active || scanning)
    {
        active      = scanning;
        currentText = mgr.getCurrentScanText();
        progress    = mgr.getScanProgress();
        repaint();
    }
    return active;
}

void ScanStatusOverlay::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto top = r.removeFromTop (24);
    cancelBtn.setBounds (top.removeFromRight (80).reduced (0, 2));
}

void ScanStatusOverlay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // Translucent dark backdrop with a lavender outline.
    g.setColour (juce::Colours::black.withAlpha (0.78f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (ns::Colours::lavender);
    g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 1.5f);

    auto inner = r.reduced (16.0f, 12.0f);

    // Title row (leave space for the Cancel button + percentage on the right).
    auto titleArea = inner.removeFromTop (24.0f);
    auto rightArea = titleArea.removeFromRight (80.0f + 8.0f + 60.0f);
    auto pctArea   = rightArea.removeFromLeft (60.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold")));
    g.drawFittedText ("SCANNING PLUGINS...", titleArea.toNearestInt(),
                      juce::Justification::centredLeft, 1);

    const int pct = juce::roundToInt (juce::jlimit (0.0f, 1.0f, progress) * 100.0f);
    g.drawFittedText (juce::String (pct) + "%", pctArea.toNearestInt(),
                      juce::Justification::centredRight, 1);

    inner.removeFromTop (4.0f);

    // Progress bar.
    auto barArea = inner.removeFromTop (6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillRoundedRectangle (barArea, 3.0f);
    auto fill = barArea.withWidth (barArea.getWidth() * juce::jlimit (0.0f, 1.0f, progress));
    g.setColour (ns::Colours::tealLight);
    g.fillRoundedRectangle (fill, 3.0f);

    inner.removeFromTop (8.0f);

    // Current file/path.
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (ns::Fonts::small());
    auto txt = currentText.isEmpty() ? juce::String ("Initialising...") : currentText;
    g.drawFittedText (txt, inner.toNearestInt(),
                      juce::Justification::centredLeft, 2,
                      0.85f);
}
