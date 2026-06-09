#include "SweetSpotMeterBar.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"
#include "../../Audio/FX/HitZoneDSP.h"

SweetSpotMeterBar::SweetSpotMeterBar()
{
    startTimerHz (30);
}

void SweetSpotMeterBar::timerCallback()
{
    const float v = App::get().getAudioEngine().getSweetSpot().currentRmsDb.load();
    rmsDbSmoothed = rmsDbSmoothed * 0.7f + v * 0.3f;
    repaint();
}

void SweetSpotMeterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = juce::jmin (12.0f, r.getHeight() * 0.5f);

    // Outer panel.
    g.setColour (ns::Colours::lavender);
    g.fillRoundedRectangle (r, radius);

    // Inset track.
    auto track = r.reduced (8.0f, 6.0f);
    const float trackR = juce::jmin (10.0f, track.getHeight() * 0.5f);

    // Three zones — TOO COLD (blue) 30%, PERFECT (green) 40%, TOO HOT (red) 30%.
    const float w = track.getWidth();
    auto cold = track.withWidth (w * 0.30f);
    auto good = track.withX (cold.getRight()).withWidth (w * 0.40f);
    auto hot  = track.withX (good.getRight()).withWidth (w - cold.getWidth() - good.getWidth());

    juce::Path coldP, goodP, hotP;
    {
        // Round just the outer corners of cold/hot bands.
        coldP.addRoundedRectangle (cold.getX(), cold.getY(),
                                   cold.getWidth(), cold.getHeight(),
                                   trackR, trackR, true, false, true, false);
        goodP.addRectangle (good);
        hotP .addRoundedRectangle (hot.getX(), hot.getY(),
                                   hot.getWidth(), hot.getHeight(),
                                   trackR, trackR, false, true, false, true);
    }

    g.setColour (juce::Colour (0xFF4A6CC9).withAlpha (0.85f));   // cold blue
    g.fillPath (coldP);
    g.setColour (ns::Colours::green.withAlpha (0.85f));          // perfect green
    g.fillPath (goodP);
    g.setColour (ns::Colours::red.withAlpha (0.85f));            // too-hot red
    g.fillPath (hotP);

    // Inner outline.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (track, trackR, 1.0f);

    // Map -60..0 dB to track position, with -16 dB sitting in the centre of the green zone.
    // -60 -> 0,  -32 -> 0.30 (cold/perfect line),  -16 -> 0.50 (centre),
    //   0 -> 0.70 (perfect/hot line),  +6 -> 0.85.
    auto dbToX = [&] (float db) -> float
    {
        // Two-segment linear: [-60..-32]→[0..0.3], [-32..-16]→[0.3..0.5],
        //                     [-16..0]→[0.5..0.7],  [0..+6]→[0.7..1.0]
        float t;
        if      (db <= -32.0f) t = juce::jmap (db, -60.0f, -32.0f, 0.0f, 0.30f);
        else if (db <= -16.0f) t = juce::jmap (db, -32.0f, -16.0f, 0.30f, 0.50f);
        else if (db <=   0.0f) t = juce::jmap (db, -16.0f,   0.0f, 0.50f, 0.70f);
        else                   t = juce::jmap (db,   0.0f,   6.0f, 0.70f, 1.00f);
        t = juce::jlimit (0.0f, 1.0f, t);
        return track.getX() + t * track.getWidth();
    };

    const float nx = dbToX (rmsDbSmoothed);

    // Needle.
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (nx - 1.5f, track.getY() - 2.0f,
                            3.0f, track.getHeight() + 4.0f, 1.5f);

    // Tick labels (small).
    g.setColour (ns::Colours::textOnPanel.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    g.drawText ("TOO COLD", cold.toNearestInt(), juce::Justification::centred);
    g.drawText ("PERFECT",  good.toNearestInt(), juce::Justification::centred);
    g.drawText ("TOO HOT",  hot .toNearestInt(), juce::Justification::centred);
}
