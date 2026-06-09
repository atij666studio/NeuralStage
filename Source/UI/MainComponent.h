#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Panels/AmpKnobsPanel.h"
#include "Panels/NamLcdPanel.h"
#include "Panels/SideRailPanel.h"
#include "Panels/TopExtrasPanel.h"
#include "Panels/TunerPanel.h"
#include "Bars/SignalChainBar.h"
#include "Bars/SceneBar.h"
#include "Bars/SweetSpotMeterBar.h"
#include "Bars/ScanStatusOverlay.h"
#include "Components/SpectrumOverlay.h"

class MainComponent : public juce::Component,
                      public juce::KeyListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint   (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    /** Update the SceneBar's "lit" button without recalling the scene.
     *  Used by App at boot to restore the previously-selected scene's
     *  visual indicator after the audio chain was already loaded from
     *  the autosaved LastChain file. */
    void setActiveSceneIndicator (int idx) { sceneBar.setActiveScene (idx); }

    /** Refresh all four SCENE button labels from the SceneManager and light
     *  the given active scene. Called after a preset that embeds its own
     *  scene bank is loaded, so the strip reflects the new preset's scenes.
     *  Pass -1 to leave the active highlight unchanged. */
    void refreshSceneStrip (int activeIdx);

private:
    void timerCallback() override;
    void handleBlockClicked (int blockId, juce::Rectangle<int> screenRect);
    void refreshABButton();
    void refreshAllFromEngine();
    void showTapMenu();
    void showABMenu();
    void showPresetsMenu();

    // Setup-menu helpers (right-click on SETUP button)
    void exportProjectBundle();
    void importProjectBundle();
    void saveDiagnosticsZip();
    void openLogFolder();

    // NAM block can also host a regular amp-sim plugin (Archetype etc.).
    // When a hosted plugin is present, NAM models are bypassed in routing.
    void loadHostedNamPlugin (const juce::PluginDescription& desc);
    void openHostedNamEditor();
    juce::PopupMenu buildHostedNamLoadMenu();

public:
    void mouseDown (const juce::MouseEvent&) override;

private:

    juce::Label    leftBrandLabel;   // "NEURAL"
    juce::Label    rightBrandLabel;  // "STAGE"
    juce::Label    cpuLabel;
    juce::TextButton rescanBtn  { "SCAN" };   // vertical, left edge of signal chain
    juce::TextButton chainEditBtn { "EDIT" }; // vertical, right edge of signal chain

    AmpKnobsPanel  ampKnobs;
    NamLcdPanel    namLcd;
    SideRailPanel  sideRail;
    TopExtrasPanel topExtras;
    TunerPanel     tunerPanel;
    // Floating blue dB readout strip (peak / rms / true-peak / LUFS-I) that
    // sits between the AUTO LVL knob and the tuner panel. Defined as a
    // standalone Component in MainComponent.cpp so it can be positioned
    // geometrically independent of the tuner.
    std::unique_ptr<juce::Component> dbReadout;
    SignalChainBar    signalChainBar;
    SweetSpotMeterBar meterBar;
    SceneBar          sceneBar;
    ScanStatusOverlay scanOverlay;

    // Bottom utility strip (under the scene buttons).
    juce::TextButton setupBtn   { "SETUP"  };
    juce::TextButton presetsBtn { "PRESETS" };
    juce::TextButton abBtn      { "A" };
    juce::TextButton undoBtn    { "UNDO"   };
    juce::TextButton redoBtn    { "REDO"   };
    juce::TextButton midiBtn    { "MIDI"   };
    juce::TextButton tapBtn     { "TAP"    };
    juce::TextButton specBtn    { "SPEC"   };

    // Pulled out of the SETUP right-click menu so they sit visibly under the
    // NEURAL / STAGE brand text (no hidden right-click affordances).
    juce::TextButton looperBtn  { "LOOPER" };
    juce::TextButton backingBtn { "BACKING TRACK" };

    std::unique_ptr<SpectrumOverlay> spectrumOverlay;

    // Loading overlay: shown while background state-push operations are in
    // flight (scene recall with plugin state reloads).  Prevents the user from
    // switching scenes before all plugin states have been absorbed.
    class LoadingOverlay : public juce::Component
    {
    public:
        LoadingOverlay()  { setInterceptsMouseClicks (true, true); }

        void update (int active, int total) noexcept
        {
            activeCount = active;
            totalCount  = std::max (total, active); // guard against 0/reset
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            // Dim the entire UI
            g.fillAll (juce::Colour (0, 0, 0).withAlpha (0.72f));

            const auto bounds = getLocalBounds().toFloat();
            const float cx = bounds.getCentreX();
            const float cy = bounds.getCentreY();

            // Progress fraction (0..1)
            const float done = (float) std::max (0, totalCount - activeCount);
            const float frac = (totalCount > 0) ? juce::jlimit (0.0f, 1.0f, done / (float) totalCount) : 0.0f;

            // ---- Main text ----
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
            g.drawText ("Loading plugins, please wait...",
                        juce::Rectangle<float> (cx - 200.0f, cy - 30.0f, 400.0f, 30.0f),
                        juce::Justification::centred, false);

            // ---- Progress bar ----
            const float barW = 320.0f;
            const float barH = 12.0f;
            const float barX = cx - barW * 0.5f;
            const float barY = cy + 12.0f;
            const juce::Rectangle<float> track (barX, barY, barW, barH);
            g.setColour (juce::Colour (60, 60, 60));
            g.fillRoundedRectangle (track, barH * 0.5f);
            if (frac > 0.0f)
            {
                g.setColour (juce::Colour (0, 160, 220));
                g.fillRoundedRectangle (track.withWidth (barW * frac), barH * 0.5f);
            }
            g.setColour (juce::Colour (120, 120, 120));
            g.drawRoundedRectangle (track, barH * 0.5f, 1.0f);

            // ---- Counter text ----
            g.setColour (juce::Colour (180, 180, 180));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            const int numDone = (int) done;
            g.drawText (juce::String (numDone) + " / " + juce::String (totalCount) + " done",
                        juce::Rectangle<float> (cx - 80.0f, barY + barH + 6.0f, 160.0f, 18.0f),
                        juce::Justification::centred, false);
        }

    private:
        int activeCount { 0 };
        int totalCount  { 0 };
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoadingOverlay)
    } loadingOverlay;

    // True after the very first startup loading batch completes.
    // Prevents the overlay from re-appearing on every scene switch.
    bool startupOverlayDismissed = false;

    std::unique_ptr<juce::FileChooser> chooser;

    // App-wide tooltip popup. Without one, all setTooltip() calls are inert.
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE (MainComponent)
};
