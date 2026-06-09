#include "SpectrumOverlay.h"
#include "../Styles/Colours.h"

SpectrumOverlay::SpectrumOverlay (SpectrumTap& t)
    : tap (t)
{
    setInterceptsMouseClicks (true, false);
    for (auto& b : bins)  b  = -80.0f;
    for (auto& p : peaks) p  = -80.0f;
}

void SpectrumOverlay::setVisible (bool shouldBeVisible)
{
    juce::Component::setVisible (shouldBeVisible);
    if (shouldBeVisible)
    {
        for (auto& b : bins)  b  = -80.0f;
        for (auto& p : peaks) p  = -80.0f;
        for (auto& a : peakAgeBlocks) a = 0;
        startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

bool SpectrumOverlay::hitTest (int, int)
{
    return isVisible(); // hidden -> mouse passes through
}

void SpectrumOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (closeButton.contains (e.getPosition()))
    {
        if (onCloseRequested) onCloseRequested();
    }
}

void SpectrumOverlay::timerCallback()
{
    std::array<float, kNumBins> fresh {};
    if (! tap.produceMagnitudes (fresh.data(), kNumBins, -80.0f, 0.0f))
        return;

    // Smooth display: rise fast, decay slow.
    for (int i = 0; i < kNumBins; ++i)
    {
        const float target = fresh[(size_t) i];
        const float prev   = bins[(size_t) i];
        bins[(size_t) i] = target > prev ? target
                                         : prev + (target - prev) * 0.35f;

        if (bins[(size_t) i] > peaks[(size_t) i])
        {
            peaks[(size_t) i]         = bins[(size_t) i];
            peakAgeBlocks[(size_t) i] = 0;
        }
        else
        {
            ++peakAgeBlocks[(size_t) i];
            if (peakAgeBlocks[(size_t) i] > 18) // ~600 ms hold @ 30 Hz
                peaks[(size_t) i] = juce::jmax (-80.0f, peaks[(size_t) i] - 1.5f);
        }
    }
    repaint();
}

void SpectrumOverlay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Dim backdrop so the overlay is readable on top of any panel.
    g.setColour (juce::Colour (0xee101218));
    g.fillRoundedRectangle (bounds.reduced (2.0f), 10.0f);
    g.setColour (juce::Colour (0xff2a3140));
    g.drawRoundedRectangle (bounds.reduced (2.0f), 10.0f, 1.2f);

    // Title + close.
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
    g.drawText ("SPECTRUM  20 Hz .. 20 kHz", bounds.reduced (14.0f, 8.0f),
                juce::Justification::topLeft);

    closeButton = juce::Rectangle<int> ((int) bounds.getRight() - 30, 8, 22, 22);
    g.setColour (juce::Colour (0xff2a3140));
    g.fillRoundedRectangle (closeButton.toFloat(), 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawText ("X", closeButton, juce::Justification::centred);

    // Grid: dB markers at 0, -20, -40, -60.
    const auto plot = bounds.reduced (14.0f, 30.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (int dbMark = 0; dbMark >= -80; dbMark -= 20)
    {
        const float y = juce::jmap ((float) dbMark, 0.0f, -80.0f,
                                     plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
    }
    // Vertical decade markers (100, 1k, 10k).
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    const float fLo = 20.0f;
    const float fHi = juce::jmin (20000.0f, (float) (tap.getSampleRate() * 0.5));
    const float logLo = std::log10 (fLo);
    const float logHi = std::log10 (fHi);
    auto freqToX = [&] (float hz)
    {
        const float t = (std::log10 (hz) - logLo) / (logHi - logLo);
        return juce::jmap (t, plot.getX(), plot.getRight());
    };
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        if (f > fLo && f < fHi)
            g.drawVerticalLine ((int) freqToX (f), plot.getY(), plot.getBottom());

    // Bars.
    const float barW = plot.getWidth() / (float) kNumBins;
    for (int i = 0; i < kNumBins; ++i)
    {
        const float dbVal  = bins[(size_t) i];
        const float dbPeak = peaks[(size_t) i];
        const float h      = juce::jmap (juce::jlimit (-80.0f, 0.0f, dbVal),
                                         -80.0f, 0.0f, 0.0f, plot.getHeight());
        const float pH     = juce::jmap (juce::jlimit (-80.0f, 0.0f, dbPeak),
                                         -80.0f, 0.0f, 0.0f, plot.getHeight());

        const float x = plot.getX() + barW * (float) i;
        const float y = plot.getBottom() - h;

        // Colour ramp: low (cool) -> high (warm).
        const float t = juce::jlimit (0.0f, 1.0f, h / plot.getHeight());
        const auto col = juce::Colour::fromHSV (juce::jmap (t, 0.55f, 0.02f),
                                                 0.75f, 0.95f, 0.92f);
        g.setColour (col);
        g.fillRect (x, y, juce::jmax (1.0f, barW - 1.0f), h);

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawHorizontalLine ((int) (plot.getBottom() - pH),
                              x, x + juce::jmax (1.0f, barW - 1.0f));
    }

    // Axis labels.
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (10.5f);
    for (auto pair : std::initializer_list<std::pair<float, const char*>>{
            {100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"} })
    {
        if (pair.first > fLo && pair.first < fHi)
        {
            const float x = freqToX (pair.first);
            g.drawText (pair.second,
                        juce::Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 12),
                        juce::Justification::centred);
        }
    }
}
