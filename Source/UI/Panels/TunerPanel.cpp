#include "TunerPanel.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"
#include "../../Audio/Tuner/TunerProcessor.h"
#include "../../MIDI/MIDILearn.h"

//==============================================================================
class TunerPanel::RoundMuteButton : public juce::Button
{
public:
    RoundMuteButton() : juce::Button ("mute")
    {
        setClickingTogglesState (true);
        setTooltip ("Output mute — silences amp/speaker for silent tuning. Right-click to assign MIDI.");
    }

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();

        // Soft drop shadow so it sits above the lavender panel.
        {
            juce::DropShadow ds (juce::Colours::black.withAlpha (0.35f), 4, { 0, 1 });
            juce::Path p; p.addEllipse (r);
            ds.drawForPath (g, p);
        }

        // Disc body — matches the tuner's charcoal disc when off, red when on.
        juce::Colour fill = on ? juce::Colour (0xffd03a3a) : juce::Colour (0xFF2A2C30);
        if (isDown)      fill = fill.brighter (0.10f);
        else if (isOver) fill = fill.brighter (0.05f);
        g.setColour (fill);
        g.fillEllipse (r);

        // Thin ring matching the tuner's green/red theme.
        g.setColour (on ? juce::Colour (0xffffb1b1) : ns::Colours::green.withAlpha (0.55f));
        g.drawEllipse (r.reduced (1.0f), 1.2f);

        // Speaker-with-slash glyph.
        const float cx = r.getCentreX(), cy = r.getCentreY();
        const float s  = juce::jmin (r.getWidth(), r.getHeight()) * 0.30f;
        juce::Path icon;
        icon.startNewSubPath (cx - s * 0.9f, cy - s * 0.45f);
        icon.lineTo          (cx - s * 0.3f, cy - s * 0.45f);
        icon.lineTo          (cx + s * 0.2f, cy - s * 0.95f);
        icon.lineTo          (cx + s * 0.2f, cy + s * 0.95f);
        icon.lineTo          (cx - s * 0.3f, cy + s * 0.45f);
        icon.lineTo          (cx - s * 0.9f, cy + s * 0.45f);
        icon.closeSubPath();
        g.setColour (juce::Colours::white);
        g.fillPath (icon);
        if (on)
        {
            g.setColour (juce::Colours::white);
            g.drawLine (cx - s * 1.0f, cy - s * 1.0f,
                        cx + s * 1.0f, cy + s * 1.0f, 1.6f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
        {
            showMidiMenu();
            return;
        }
        juce::Button::mouseDown (e);
    }

private:
    void showMidiMenu()
    {
        const juce::String paramId = "output.mute";
        auto& reg = App::get().getMIDILearn();

        juce::PopupMenu m;
        m.addSectionHeader ("Output Mute");

        juce::String existing;
        for (auto& mp : reg.getMappings())
        {
            if (mp.paramId == paramId)
            {
                existing = ((mp.type == MidiMsgType::CC)   ? juce::String ("CC ")
                          : (mp.type == MidiMsgType::Note) ? juce::String ("Note ")
                                                           : juce::String ("PC "))
                         + juce::String (mp.ccOrNote)
                         + (mp.channel == 0 ? " (Omni)" : " Ch " + juce::String (mp.channel));
                break;
            }
        }

        if (existing.isNotEmpty())
            m.addItem ("Mapped: " + existing, false, false, [] {});

        if (reg.isLearning() && reg.currentLearnTarget() == paramId)
            m.addItem ("Cancel learn", [&reg] { reg.cancelLearn(); });
        else
            m.addItem ("MIDI Learn (move a controller)", [&reg] { reg.beginLearn ("output.mute"); });

        m.addItem ("Clear MIDI mapping", existing.isNotEmpty(), false,
                   [&reg] { reg.clearMapping ("output.mute"); });

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }
};

TunerPanel::TunerPanel()
{
    muteBtn = std::make_unique<RoundMuteButton>();

    // Reflect current engine state (App::initialise mutes input on launch so
    // the button should already show the muted/red glyph at startup).
    {
        auto& eng = App::get().getAudioEngine();
        muteBtn->setToggleState (eng.getOutput().isMuted(), juce::dontSendNotification);
    }

    muteBtn->onClick = [this]
    {
        const bool m = muteBtn->getToggleState();
        App::get().getAudioEngine().getOutput().setMute (m);
    };
    addAndMakeVisible (*muteBtn);

    startTimerHz (20);
}

TunerPanel::~TunerPanel() = default;

void TunerPanel::timerCallback()
{
    // Keep button visual in sync with engine mute state (handles MIDI-triggered mutes).
    {
        const bool engineMuted = App::get().getAudioEngine().getOutput().isMuted();
        if (muteBtn->getToggleState() != engineMuted)
            muteBtn->setToggleState (engineMuted, juce::dontSendNotification);
    }

    auto& tn = App::get().getAudioEngine().getTuner();
    int n = -1; float c = 0.0f;
    tn.getNoteAndCents (n, c);
    const float f = tn.getFrequencyHz();
    const float conf = tn.getConfidence();

    if (conf > 0.4f && n > 0)
    {
        smoothedNote  = n;
        smoothedCents = smoothedCents * 0.6f + c * 0.4f;
        smoothedFreq  = smoothedFreq  * 0.6f + f * 0.4f;
    }
    else
    {
        smoothedFreq *= 0.9f;
    }

    // -------- Auto-mute (silent tuning) --------
    // Timer runs at 20 Hz. Mute when the SAME note is held confidently for
    // ~250 ms (5 ticks); release when the input goes quiet for ~250 ms.
    if (autoMuteEnabled)
    {
        const bool confident = (conf > 0.5f && n > 0);
        if (confident && n == lastStableNote)
        {
            ++stableTicks;
            silenceTicks = 0;
        }
        else if (confident)
        {
            lastStableNote = n;
            stableTicks    = 1;
            silenceTicks   = 0;
        }
        else
        {
            stableTicks = 0;
            ++silenceTicks;
        }

        auto& eng = App::get().getAudioEngine();
        if (! autoMuteActive && stableTicks >= 5)
        {
            eng.getOutput().setMute (true);
            autoMuteActive = true;
            muteBtn->setToggleState (true, juce::dontSendNotification);
        }
        else if (autoMuteActive && silenceTicks >= 5)
        {
            eng.getOutput().setMute (false);
            autoMuteActive = false;
            muteBtn->setToggleState (false, juce::dontSendNotification);
        }
    }
    else
    {
        autoMuteActive = false;
        stableTicks    = 0;
        silenceTicks   = 0;
    }

    // GR LED glow: peak-hold-ish. Map 0..-6 dB -> 0..1.
    const float grDb  = App::get().getAudioEngine().getOutput().getSafetyReductionDb(); // <= 0
    const float grNew = juce::jlimit (0.0f, 1.0f, -grDb / 6.0f);
    limGlow = juce::jmax (grNew, limGlow * 0.80f); // fast attack, ~150 ms decay at 20 Hz

    // Output meter: smoothed peak-dB level (-60..0).
    {
        const float pk = App::get().getAudioEngine().getOutput().getOutputPeakDb();
        // Map -60..0 dB to 0..1.
        const float t  = juce::jlimit (0.0f, 1.0f, (pk + 60.0f) / 60.0f);
        meterLevel = juce::jmax (t, meterLevel * 0.85f); // fast attack, ~250 ms decay
    }

    repaint();
}

void TunerPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Lavender outer panel (full panel area).
    g.setColour (ns::Colours::lavender);
    g.fillRoundedRectangle (bounds, (float) ns::UI::kPanelRadius);

    // Title
    g.setColour (ns::Colours::textOnPanel);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    g.drawFittedText ("TUNER",
                      bounds.removeFromTop (18.0f).toNearestInt(),
                      juce::Justification::centred, 1);

    // Charcoal disc.
    auto disc = bounds.reduced (10.0f);
    const float side = juce::jmin (disc.getWidth(), disc.getHeight() - 6.0f);
    auto circle = juce::Rectangle<float> (0, 0, side, side)
                      .withCentre ({ disc.getCentreX(), disc.getCentreY() });

    // soft shadow
    {
        juce::DropShadow ds (juce::Colours::black.withAlpha (0.45f), 8, { 0, 3 });
        juce::Path cp; cp.addEllipse (circle);
        ds.drawForPath (g, cp);
    }
    g.setColour (juce::Colour (0xFF2A2C30));
    g.fillEllipse (circle);

    const float cx = circle.getCentreX();
    const float cy = circle.getCentreY();
    const float r  = side * 0.5f;

    // Accuracy semicircle: green left, red right, with needle.
    const float arcInner = r * 0.78f;
    const float arcThick = juce::jmax (4.0f, r * 0.10f);

    auto strokeArc = [&] (float a0, float a1, juce::Colour col)
    {
        juce::Path p;
        p.addCentredArc (cx, cy, arcInner, arcInner, 0.0f, a0, a1, true);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (arcThick,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    const float startA = juce::degreesToRadians (-110.0f);
    const float endA   = juce::degreesToRadians ( 110.0f);
    strokeArc (startA, 0.0f, ns::Colours::green);
    strokeArc (0.0f,   endA, ns::Colours::red);

    // Needle (cents → angle within ±50¢).
    if (smoothedNote > 0)
    {
        const float t = juce::jlimit (-1.0f, 1.0f, smoothedCents / 50.0f);
        const float ang = t * endA;
        const float nx = cx + std::sin (ang) * arcInner;
        const float ny = cy - std::cos (ang) * arcInner;
        g.setColour (juce::Colours::white);
        g.drawLine (cx, cy, nx, ny, 2.0f);
        g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    }

    // Note glyph (big green) + frequency.
    g.setColour (ns::Colours::green);
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (28.0f, r * 0.55f)).withStyle ("Bold")));
    const auto noteTxt = TunerProcessor::noteName (smoothedNote);
    g.drawFittedText (noteTxt.isEmpty() ? juce::String ("--") : noteTxt,
                      juce::Rectangle<int> ((int) (cx - r), (int) (cy - r * 0.55f),
                                            (int) (r * 2.0f), (int) (r * 0.7f)),
                      juce::Justification::centred, 1);

    g.setFont (juce::Font (juce::FontOptions (juce::jmax (10.0f, r * 0.18f))));
    const juce::String freqTxt = (smoothedFreq > 0.5f)
                                    ? juce::String (smoothedFreq, 1) + " Hz"
                                    : juce::String ("-- Hz");
    g.drawFittedText (freqTxt,
                      juce::Rectangle<int> ((int) (cx - r), (int) (cy + r * 0.20f),
                                            (int) (r * 2.0f), (int) (r * 0.30f)),
                      juce::Justification::centred, 1);

    // -------- Output level meter (thin horizontal bar inside disc) --------
    {
        const float barW = r * 1.30f;
        const float barH = juce::jmax (4.0f, r * 0.07f);
        juce::Rectangle<float> bar (cx - barW * 0.5f,
                                    cy + r * 0.55f,
                                    barW, barH);

        // Track.
        g.setColour (juce::Colour (0xFF1B1D20));
        g.fillRoundedRectangle (bar, barH * 0.5f);

        // Fill: green up to -12 dB (~0.8), amber to -3 dB (~0.95), red above.
        const float lvl = juce::jlimit (0.0f, 1.0f, meterLevel);
        if (lvl > 0.001f)
        {
            auto fill = bar.withWidth (bar.getWidth() * lvl);

            juce::ColourGradient grad (ns::Colours::green,        bar.getX(), 0.0f,
                                       juce::Colour (0xffd03a3a), bar.getRight(), 0.0f, false);
            grad.addColour (0.80, juce::Colour (0xffe6b400)); // amber
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, barH * 0.5f);
        }

        // Slow peak-hold marker (a thin vertical line at the held level).
        const float holdDb = App::get().getAudioEngine().getOutput().getOutputPeakHoldDb();
        const float holdT  = juce::jlimit (0.0f, 1.0f, (holdDb + 60.0f) / 60.0f);
        if (holdT > 0.01f)
        {
            const float hx = bar.getX() + bar.getWidth() * holdT;
            juce::Colour holdCol = (holdT > 0.95f) ? juce::Colour (0xffd03a3a)
                                : (holdT > 0.80f) ? juce::Colour (0xffe6b400)
                                                  : juce::Colours::white;
            g.setColour (holdCol.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (hx - 1.0f, bar.getY() - 1.0f,
                                                2.0f, bar.getHeight() + 2.0f));
        }

        // Outline.
        g.setColour (juce::Colour (0xff707378).withAlpha (0.6f));
        g.drawRoundedRectangle (bar.reduced (0.4f), barH * 0.5f, 0.8f);

        // -12 dB tick (mid reference).
        const float tickX = bar.getX() + bar.getWidth() * 0.80f;
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (tickX, bar.getY() + 1.0f, tickX, bar.getBottom() - 1.0f, 1.0f);

        // Clip LED: small dot to the right of the bar. Latches red on overload,
        // click to reset. Dim grey when clean.
        const float ledD = barH * 1.4f;
        juce::Rectangle<float> led (bar.getRight() + 4.0f,
                                    bar.getCentreY() - ledD * 0.5f,
                                    ledD, ledD);
        const bool clipped = App::get().getAudioEngine().getOutput().hasClipped();
        if (clipped)
        {
            g.setColour (juce::Colour (0xffd03a3a).withAlpha (0.35f));
            g.fillEllipse (led.expanded (1.5f));
        }
        g.setColour (clipped ? juce::Colour (0xffff5555)
                             : juce::Colour (0xff3a3d42));
        g.fillEllipse (led);
        g.setColour (juce::Colour (0xff707378));
        g.drawEllipse (led, 0.8f);
        clipLedBounds = led.toNearestInt();

        // Numeric dB readout has been moved to the top of the panel
        // (above the TUNER title) for legibility — see top of paint().
    }

    // -------- Safety-limiter GR pill --------
    // Mirror the mute button's tangent position to the lower-right of the disc.
    {
        const float btn   = juce::jlimit (22.0f, 30.0f, r * 0.32f);
        const float ang   = juce::degreesToRadians (45.0f);
        const float ox    = cx + std::cos (ang) * (r + btn * 0.20f);
        const float oy    = cy + std::sin (ang) * (r + btn * 0.20f);
        const float pillW = btn * 1.35f;
        const float pillH = btn * 0.62f;
        juce::Rectangle<float> pill (ox - pillW * 0.5f, oy - pillH * 0.5f, pillW, pillH);

        const auto& out = App::get().getAudioEngine().getOutput();
        const bool  on  = out.isSafetyLimiterEnabled();

        // Body — dim charcoal, glows red as limGlow rises.
        juce::Colour body = juce::Colour (0xFF2A2C30);
        if (on && limGlow > 0.001f)
        {
            const float a = juce::jlimit (0.0f, 1.0f, 0.25f + limGlow * 0.75f);
            body = juce::Colour (0xffd03a3a).withAlpha (a).overlaidWith (body);
        }
        g.setColour (body);
        g.fillRoundedRectangle (pill, pillH * 0.5f);

        // Ring — green when armed (no GR), red when reducing, grey when disabled.
        juce::Colour ring = on ? (limGlow > 0.001f ? juce::Colour (0xffffb1b1)
                                                   : ns::Colours::green.withAlpha (0.55f))
                               : juce::Colour (0xff707378);
        g.setColour (ring);
        g.drawRoundedRectangle (pill.reduced (0.6f), pillH * 0.5f, 1.1f);

        // Label.
        g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.55f));
        g.setFont (juce::Font (juce::FontOptions (pillH * 0.50f).withStyle ("Bold")));
        g.drawFittedText ("LIM", pill.toNearestInt(), juce::Justification::centred, 1);

        // Cache for hit-test in mouseDown.
        limPillBounds = pill.toNearestInt();
    }
}

void TunerPanel::resized()
{
    // Recompute the disc geometry the same way paint() does, then place
    // the round mute button hugging the upper-right of the disc.
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop (18.0f);                         // TUNER title strip
    auto disc = bounds.reduced (10.0f);
    const float side = juce::jmin (disc.getWidth(), disc.getHeight() - 6.0f);
    auto circle = juce::Rectangle<float> (0, 0, side, side)
                       .withCentre ({ disc.getCentreX(), disc.getCentreY() });
    const float r  = side * 0.5f;
    const float btn = juce::jlimit (22.0f, 30.0f, r * 0.32f);

    // Position: 45° above-right, tangent to the disc.
    const float angle = juce::degreesToRadians (-45.0f);
    const float ox = circle.getCentreX() + std::cos (angle) * (r + btn * 0.20f);
    const float oy = circle.getCentreY() + std::sin (angle) * (r + btn * 0.20f);
    muteBtn->setBounds (juce::Rectangle<float> (ox - btn * 0.5f, oy - btn * 0.5f,
                                                btn, btn).toNearestInt());
}

void TunerPanel::mouseDown (const juce::MouseEvent& e)
{
    // Right-click anywhere on the panel: tuner options menu (currently just
    // the auto-mute toggle; future: tuning reference, transpose, etc.).
    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu m;
        m.addSectionHeader ("TUNER");
        m.addItem (1, "Auto-mute when tuning", true, autoMuteEnabled);
        m.showMenuAsync (juce::PopupMenu::Options()
                           .withTargetComponent (this)
                           .withParentComponent (getTopLevelComponent()),
                         [this] (int choice)
                         {
                             if (choice == 1)
                             {
                                 autoMuteEnabled = ! autoMuteEnabled;
                                 stableTicks  = 0;
                                 silenceTicks = 0;
                                 if (! autoMuteEnabled && autoMuteActive)
                                 {
                                     App::get().getAudioEngine().getOutput().setMute (false);
                                     muteBtn->setToggleState (false, juce::dontSendNotification);
                                     autoMuteActive = false;
                                 }
                             }
                         });
        return;
    }

    if (! limPillBounds.isEmpty() && limPillBounds.contains (e.getPosition()))
    {
        auto& out = App::get().getAudioEngine().getOutput();
        const bool now = ! out.isSafetyLimiterEnabled();
        out.setSafetyLimiterEnabled (now);
        repaint();
        return;
    }
    if (! clipLedBounds.isEmpty() && clipLedBounds.contains (e.getPosition()))
    {
        // Left-click clears clip; right-click also resets the integrated-LUFS window.
        auto& out = App::get().getAudioEngine().getOutput();
        out.clearClip();
        if (e.mods.isRightButtonDown())
            out.resetIntegratedLoudness();
        repaint();
        return;
    }
    juce::Component::mouseDown (e);
}
