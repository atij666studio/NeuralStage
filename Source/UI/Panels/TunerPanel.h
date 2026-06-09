#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Bottom-right tuner display, NAM-A2 style.
 *  Charcoal disc with green note glyph, frequency below, semicircle accuracy arc.
 */
class TunerPanel : public juce::Component,
                   private juce::Timer
{
public:
    TunerPanel();
    ~TunerPanel() override;

    void paint   (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    int   smoothedNote { -1 };
    float smoothedCents { 0.0f };
    float smoothedFreq  { 0.0f };

    // Safety-limiter gain-reduction (0..1, smoothed for the LED glow).
    float limGlow { 0.0f };

    // Output level meter (0..1, post-limiter peak, smoothed).
    float meterLevel { 0.0f };

    // Cached LIM-pill bounds (set in paint() so mouseDown can hit-test).
    juce::Rectangle<int> limPillBounds;
    juce::Rectangle<int> clipLedBounds;

    // Auto-mute (silent tuning) -- right-click the panel to toggle.
    // When ON and the tuner has held a stable note for >= ~250 ms the master
    // input/output are muted automatically; mute releases when the note dies.
    bool autoMuteEnabled { false };
    bool autoMuteActive  { false };   // we own the current mute (vs user-set)
    int  stableTicks     { 0 };       // consecutive ticks of same stable note
    int  silenceTicks    { 0 };       // consecutive ticks with no confident pitch
    int  lastStableNote  { -1 };

    class RoundMuteButton;
    std::unique_ptr<RoundMuteButton> muteBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerPanel)
};
