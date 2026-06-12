#include "MainComponent.h"
#include "Styles/Colours.h"
#include "Styles/Fonts.h"
#include "Styles/UIConstants.h"
#include "Panels/CategoryPopup.h"
#include "Dialogs/Dialogs.h"
#include "../App.h"
#include "../Audio/AudioEngine.h"
#include "../PluginHost/PluginManager.h"
#include "../Utils/RecentFiles.h"
#include "../Utils/FileUtils.h"
#include "../MIDI/MidiClockSender.h"

namespace
{
    /** TextButton LNF that paints letters stacked vertically, top-to-bottom
     *  (one glyph per row). Used for the SCAN / EDIT buttons that sit glued
     *  to the left and right edges of the signal chain. */
    class VertStackLNF : public juce::LookAndFeel_V4
    {
    public:
        void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                             bool /*isHi*/, bool /*isDown*/) override
        {
            const auto txt = b.getButtonText();
            const auto r   = b.getLocalBounds().reduced (2, 6);
            const int n = txt.length();
            if (n <= 0) return;

            const float maxByH = (float) r.getHeight() / (float) juce::jmax (1, n);
            const float fontH  = juce::jlimit (8.0f, 14.0f, maxByH * 0.85f);

            g.setColour (b.findColour (juce::TextButton::textColourOffId));
            g.setFont (juce::Font (juce::FontOptions (fontH).withStyle ("Bold")));

            const float rowH = (float) r.getHeight() / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const auto cellY = r.getY() + (int) std::round (i * rowH);
                juce::Rectangle<int> cell (r.getX(), cellY,
                                           r.getWidth(),
                                           (int) std::ceil (rowH));
                g.drawFittedText (juce::String::charToString (txt[i]), cell,
                                  juce::Justification::centred, 1);
            }
        }
    };

    inline VertStackLNF& vertLNF() { static VertStackLNF inst; return inst; }

    /** Floating two-line blue dB readout (peak / rms / true-peak / LUFS-I).
     *  Lives between the AUTO LVL knob and the tuner panel as its own
     *  Component so MainComponent can position it geometrically without the
     *  tuner panel having to stretch upward. Transparent background. */
    class DbReadoutBar : public juce::Component,
                         private juce::Timer
    {
    public:
        DbReadoutBar() { setInterceptsMouseClicks (false, false); startTimerHz (15); }
        void paint (juce::Graphics& g) override
        {
            const auto& out = App::get().getAudioEngine().getOutput();
            const float pk  = juce::jmax (-60.0f, out.getOutputPeakDb());
            const float rms = juce::jmax (-60.0f, out.getOutputRmsDb());
            const float tp  = juce::jmax (-60.0f, out.getOutputTruePeakDb());
            const float li  = juce::jmax (-70.0f, out.getIntegratedLoudnessDb());
            const juce::String l1 = juce::String (pk, 1)  + " pk  /  "
                                  + juce::String (rms, 1) + " rms";
            const juce::String l2 = juce::String (tp, 1)  + " tp  /  "
                                  + juce::String (li, 1)  + " LU-I";
            auto strip = getLocalBounds().reduced (2, 1);
            g.setColour (juce::Colour (0xFF4A6CC9));
            g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
            const int halfH = strip.getHeight() / 2;
            g.drawFittedText (l1, strip.removeFromTop (halfH), juce::Justification::centred, 1);
            g.drawFittedText (l2, strip,                       juce::Justification::centred, 1);
        }
        void timerCallback() override { repaint(); }
    };

    /** Compute a PopupMenu::Options that centres the menu over the active
     *  top-level app window (standalone host or DAW plugin window). */
    juce::PopupMenu::Options centredMenuOptions()
    {
        if (auto* tl = juce::TopLevelWindow::getActiveTopLevelWindow())
        {
            const auto sb = tl->getScreenBounds();
            return juce::PopupMenu::Options().withTargetScreenArea (
                juce::Rectangle<int> (sb.getCentreX(), sb.getCentreY(), 1, 1));
        }
        return juce::PopupMenu::Options();
    }
}

MainComponent::MainComponent()
{
    setSize (ns::UI::kAppWidth, ns::UI::kAppHeight);

    // Brand labels: paint with a vertical gradient matching the splash logo.
    // NEURAL = silver/white (top) → light grey (bottom).
    // STAGE  = bright lilac (top) → deep purple (bottom).
    static struct BrandLF : juce::LookAndFeel_V4
    {
        juce::Colour top, bot;
        void drawLabel (juce::Graphics& g, juce::Label& l) override
        {
            auto r = l.getLocalBounds().toFloat();
            juce::ColourGradient grad (top, r.getCentreX(), r.getY(),
                                       bot, r.getCentreX(), r.getBottom(), false);
            g.setGradientFill (grad);
            g.setFont (l.getFont());
            g.drawFittedText (l.getText(), l.getLocalBounds(),
                              l.getJustificationType(), 1);
        }
    } silverLF, purpleLF;
    silverLF.top = juce::Colour (0xFFFFFFFF);   // bright white
    silverLF.bot = juce::Colour (0xFFB8B8C2);   // cool silver-grey
    purpleLF.top = juce::Colour (0xFFC79CFF);   // light lilac highlight
    purpleLF.bot = juce::Colour (0xFF6A2BB8);   // deep purple

    auto setupBrand = [] (juce::Label& l, const juce::String& text,
                          juce::Justification j, juce::LookAndFeel& lf)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (34.0f).withStyle ("Bold")));
        l.setJustificationType (j);
        l.setLookAndFeel (&lf);
    };
    setupBrand (leftBrandLabel,  "NEURAL", juce::Justification::centred, silverLF);
    setupBrand (rightBrandLabel, "STAGE",  juce::Justification::centred, purpleLF);
    addAndMakeVisible (leftBrandLabel);
    addAndMakeVisible (rightBrandLabel);

    cpuLabel.setFont (ns::Fonts::small());
    cpuLabel.setColour (juce::Label::textColourId, ns::Colours::textSecondary);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setText ("CPU: --   |   -- samples", juce::dontSendNotification);
    addAndMakeVisible (cpuLabel);

    rescanBtn.setColour (juce::TextButton::buttonColourId,  ns::Colours::chipUnsel);
    rescanBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    {
        // Tooltip dynamically reflects which formats this build will scan and
        // whether any plugins are parked on the "needs authentication" list
        // (iLok / PACE timeouts). Updated on every popup via timerCallback.
        auto& mgr = App::get().getPluginManager();
        juce::String tip;
        tip << "Rescan installed plugins (" << mgr.getScannedFormatsDescription() << ")";
        if (const int n = mgr.getNeedsAuthCount(); n > 0)
            tip << "\n" << n << " plugin(s) need authentication — right-click for options";
        tip << "\nRight-click for advanced options";
        rescanBtn.setTooltip (tip);
    }
    rescanBtn.setLookAndFeel (&vertLNF());
    rescanBtn.onClick = []
    {
        auto& mgr = App::get().getPluginManager();
        if (mgr.isScanning()) return;
        mgr.beginAsyncScan ({}, {});
    };
    // Right-click → advanced scan options surfacing the format list, the
    // needs-auth count (with one-click clear), and a way to wipe the
    // dead-man's-pedal blacklist so previously-crashed plugins are retried.
    rescanBtn.onClick = rescanBtn.onClick; // keep left-click as plain rescan
    class ScanMenuMouse : public juce::MouseListener
    {
    public:
        explicit ScanMenuMouse (juce::Component* parent) : owner (parent) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu()) return;
            auto& mgr = App::get().getPluginManager();
            juce::PopupMenu m;
            m.addSectionHeader ("Plugin scan");
            m.addItem ("Formats: " + mgr.getScannedFormatsDescription(), false, false, [] {});
            const int needsAuth = mgr.getNeedsAuthCount();
            if (needsAuth > 0)
            {
                m.addSectionHeader (juce::String::fromUTF8 ("\xE2\x9A\xA0 ") +
                                    juce::String (needsAuth) + " plugin(s) need authentication");
                m.addItem ("Clear needs-auth list and rescan", [&mgr]
                {
                    mgr.clearNeedsAuth();
                    if (! mgr.isScanning()) mgr.beginAsyncScan ({}, {});
                });
            }
            m.addSeparator();
            m.addItem ("Rescan now", ! mgr.isScanning(), false,
                       [&mgr] { mgr.beginAsyncScan ({}, {}); });
            m.addItem ("Clear crashed-plugin blacklist", [&mgr] { mgr.clearBlacklist(); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (owner));
        }
    private:
        juce::Component* owner;
    };
    static ScanMenuMouse scanMenuMouse (&rescanBtn);
    rescanBtn.addMouseListener (&scanMenuMouse, false);
    addAndMakeVisible (rescanBtn);

    // EDIT: vertical button on the right edge of the signal chain. Opens the
    // load / remove / replace / view popup (this used to fire when the user
    // clicked the block itself; the block clicks now toggle bypass instead).
    chainEditBtn.setColour (juce::TextButton::buttonColourId,  ns::Colours::chipUnsel);
    chainEditBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    chainEditBtn.setTooltip ("Edit signal chain: load / remove / replace plugins.");
    chainEditBtn.setLookAndFeel (&vertLNF());
    chainEditBtn.onClick = [this]
    {
        auto& eng = App::get().getAudioEngine();

        struct Entry { int id; const char* label; PluginChain* chain; ns::FxCategory cat; };
        const Entry entries[] = {
            { SignalChainBar::Gate,    "GATE",   &eng.getPreFxChain(),  ns::FxCategory::Gate       },
            { SignalChainBar::Comp,    "COMP",   &eng.getPreFxChain(),  ns::FxCategory::Compressor },
            { SignalChainBar::Drive,   "DRIVE",  &eng.getPreFxChain(),  ns::FxCategory::Drive      },
            { SignalChainBar::NamAmp,  "NAM",        nullptr,               ns::FxCategory::Other      },
            { SignalChainBar::IrCab,   "IR",         &eng.getPostFxChain(), ns::FxCategory::IRLoader   },
            { SignalChainBar::Eq,      "EQ",         &eng.getPostFxChain(), ns::FxCategory::EQ         },
            { SignalChainBar::Mod,     "MOD",        (signalChainBar.isModBeforeNam() ? &eng.getPreFxChain() : &eng.getPostFxChain()), ns::FxCategory::Modulation },
            { SignalChainBar::Delay,   "DELAY",      &eng.getPostFxChain(), ns::FxCategory::Delay      },
            { SignalChainBar::Reverb,  "REVERB",     &eng.getPostFxChain(), ns::FxCategory::Reverb     },
            { SignalChainBar::Limiter, "LIMIT",      &eng.getPostFxChain(), ns::FxCategory::Limiter    },
            { SignalChainBar::Fx,      "MASTER FX",  &eng.getPostFxChain(), ns::FxCategory::Other      },
        };

        juce::PopupMenu m;
        for (const auto& e : entries)
        {
            juce::PopupMenu sub;
            int count = 0;

            if (e.id == SignalChainBar::NamAmp)
            {
                // List the 4 NAM slots; clicking jumps into the existing slot popup.
                auto& nam = eng.getNAM();
                for (int i = 0; i < 4; ++i)
                {
                    const char letter = (char) ('A' + i);
                    const auto name = nam.hasSlot (i) ? nam.getSlotName (i)
                                                      : juce::String ("(empty)");
                    if (nam.hasSlot (i)) ++count;
                    sub.addItem (juce::String::charToString ((juce::juce_wchar) letter)
                                 + ": " + name,
                                 [this] { handleBlockClicked (SignalChainBar::NamAmp,
                                                              chainEditBtn.getScreenBounds()); });
                }

                // Quick-access for the hosted amp-sim plugin (one click to
                // open its editor, no diving through the slot manager).
                sub.addSeparator();
                if (nam.hasHostedPlugin())
                {
                    ++count;
                    sub.addItem ("Plugin: " + nam.getHostedPluginName(),
                                 [this] { openHostedNamEditor(); });
                    sub.addItem ("Clear hosted plugin", [this]
                    {
                        App::get().getAudioEngine().getNAM().clearHostedPlugin();
                        signalChainBar.refreshBadges();
                    });
                }
                else
                {
                    sub.addItem ("Plugin: (none)", false, false, [] {});
                }
                sub.addSubMenu ("Load amp-sim plugin", buildHostedNamLoadMenu());
            }
            else if (e.chain != nullptr)
            {
                // List the assigned plugins in this category; clicking opens
                // the editor directly.
                const int blockId = e.id;
                auto slots = e.chain->getSlotsForUI();
                for (auto* s : slots)
                {
                    if (s == nullptr || s->instance == nullptr) continue;
                    if (s->category != e.cat) continue;
                    // The "FX" block buckets both Other and Utility.
                    if (e.id == SignalChainBar::Fx
                        && s->category != ns::FxCategory::Other
                        && s->category != ns::FxCategory::Utility) continue;
                    ++count;
                    PluginSlot* slotPtr = s;
                    sub.addItem (s->displayName,
                                 [slotPtr] { CategoryPopup::openPluginEditor (*slotPtr); });
                }
                if (e.id == SignalChainBar::Fx)
                {
                    // Also list Utility plugins under FX (the chain-bar Fx
                    // block bypasses Other+Utility together).
                    for (auto* s : slots)
                    {
                        if (s == nullptr || s->instance == nullptr) continue;
                        if (s->category != ns::FxCategory::Utility) continue;
                        ++count;
                        PluginSlot* slotPtr = s;
                        sub.addItem (s->displayName,
                                     [slotPtr] { CategoryPopup::openPluginEditor (*slotPtr); });
                    }
                }
                sub.addSeparator();
                sub.addItem ("Add / manage...",
                             [this, blockId]
                             {
                                 handleBlockClicked (blockId, chainEditBtn.getScreenBounds());
                             });
            }

            const juce::String header = juce::String (e.label)
                + (count > 0 ? "  (" + juce::String (count) + ")" : juce::String());
            m.addSubMenu (header, sub);
        }

        // Anchor the menu to the EDIT button and parent it to MainComponent
        // so the menu (and every sub-menu it spawns) is clipped to the app
        // window instead of spilling onto the desktop / next monitor.
        m.showMenuAsync (juce::PopupMenu::Options()
                           .withTargetComponent (&chainEditBtn)
                           .withParentComponent  (this));
    };
    addAndMakeVisible (chainEditBtn);

    // z-order: rails first, then content placed inside them (tuner, brand label).
    addAndMakeVisible (ampKnobs);
    addAndMakeVisible (signalChainBar);

    // Restore the MOD chip's draggable Pre/Post position from disk so the
    // user's layout choice survives across launches. Wire onChange to write
    // the file (cheap one-byte text) immediately so we don't depend on a
    // clean shutdown.
    {
        const auto modPosFile = ns::FileUtils::userDataDir().getChildFile ("ModPosition.txt");
        if (modPosFile.existsAsFile())
            signalChainBar.setModBeforeNam (modPosFile.loadFileAsString().getIntValue() != 0);

        signalChainBar.onModPositionChanged = [modPosFile] (bool beforeNam)
        {
            modPosFile.replaceWithText (beforeNam ? "1" : "0");
        };
    }
    addAndMakeVisible (sideRail);
    addAndMakeVisible (topExtras);
    addAndMakeVisible (namLcd);
    addAndMakeVisible (meterBar);
    addAndMakeVisible (sceneBar);
    addAndMakeVisible (tunerPanel);   // sits on top of left rail
    dbReadout = std::make_unique<DbReadoutBar>();
    addAndMakeVisible (*dbReadout);
    dbReadout->toFront (false);
    addAndMakeVisible (cpuLabel);
    addChildComponent (scanOverlay);   // visible only while scanning
    addChildComponent (loadingOverlay);
    // Show the overlay immediately at startup so the user sees it before the
    // very first timer tick (100ms). The timerCallback dismisses it once all
    // initial state-push batches complete.
    loadingOverlay.setVisible (true);
    loadingOverlay.toFront (false);

    // setSize() in the very first line of the ctor fired resized() before
    // dbReadout existed, so it never received bounds. Re-trigger layout
    // now that all children are in place.
    resized();

    // ---- Bottom utility strip (under scene buttons) ----
    auto styleMini = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,  ns::Colours::chipUnsel);
        b.setColour (juce::TextButton::buttonOnColourId, ns::Colours::tealAccent);
        b.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    };
    for (auto* b : { &setupBtn, &presetsBtn, &abBtn, &undoBtn, &redoBtn, &midiBtn, &tapBtn,
                     &specBtn, &looperBtn, &backingBtn })
    {
        styleMini (*b);
        addAndMakeVisible (*b);
    }

    setupBtn  .setTooltip ("Audio / MIDI Settings");
    presetsBtn.setTooltip ("Preset browser");
    abBtn     .setTooltip ("Toggle A / B compare");
    undoBtn   .setTooltip ("Undo  (\xe2\x8c\x98Z)");
    redoBtn   .setTooltip ("Redo  (\xe2\x87\xa7\xe2\x8c\x98Z)");
    midiBtn   .setTooltip ("Footswitch Wizard. Right-click for Panic / All Notes Off / full MIDI Assignments table.");
    tapBtn    .setTooltip ("Tap tempo -- click 2+ times to set BPM. Right-click for menu.");
    specBtn   .setTooltip ("Toggle spectrum analyser overlay");
    specBtn   .setClickingTogglesState (true);
    specBtn   .onClick = [this]
    {
        if (spectrumOverlay == nullptr)
        {
            spectrumOverlay = std::make_unique<SpectrumOverlay> (App::get().getAudioEngine().getSpectrumTap());
            spectrumOverlay->onCloseRequested = [this]
            {
                specBtn.setToggleState (false, juce::dontSendNotification);
                if (spectrumOverlay) spectrumOverlay->setVisible (false);
            };
            addChildComponent (*spectrumOverlay);
            resized();
        }
        spectrumOverlay->setVisible (specBtn.getToggleState());
        if (spectrumOverlay->isVisible()) spectrumOverlay->toFront (false);
    };
    looperBtn .setTooltip ("Open the looper window.");
    backingBtn.setTooltip ("Load and play a backing track.");

    setupBtn  .onClick = []  { ns::Dialogs::showAudioMidiSettings(); };
    presetsBtn.onClick = [this] { showPresetsMenu(); };
    midiBtn   .onClick = []  { ns::Dialogs::showFootswitchWizard(); };
    looperBtn .onClick = []  { ns::Dialogs::showLooperDialog(); };
    backingBtn.onClick = []  { ns::Dialogs::showBackingTrackDialog(); };
    undoBtn   .onClick = [this] { App::get().undo(); refreshAllFromEngine(); };
    redoBtn   .onClick = [this] { App::get().redo(); refreshAllFromEngine(); };
    abBtn     .onClick = [this]
    {
        auto cur = App::get().getActiveAB();
        App::get().setActiveAB (cur == App::ABSlot::A ? App::ABSlot::B : App::ABSlot::A);
        refreshAllFromEngine();
    };
    tapBtn    .onClick = [this]
    {
        App::get().getAudioEngine().getTempoClock().tap();
        // Update label immediately for snappy feedback.
        const auto bpm = App::get().getAudioEngine().getTempoClock().getBpm();
        tapBtn.setButtonText (juce::String (bpm, 1) + " BPM");
    };
    tapBtn.addMouseListener (this, false);
    abBtn .addMouseListener (this, false);
    midiBtn.addMouseListener (this, false);
    setupBtn.addMouseListener (this, false);
    setupBtn.setTooltip ("Audio / MIDI Settings -- right-click for Noise Gate / Offline Render Stems");
    leftBrandLabel .addMouseListener (this, false);
    rightBrandLabel.addMouseListener (this, false);
    leftBrandLabel .setTooltip ("About NeuralStage");
    rightBrandLabel.setTooltip ("About NeuralStage");
    // The scene bar's bounds span the gaps either side of the four scene
    // buttons too, and it was added to the parent after the brand labels,
    // so it sat above them in z-order and ate their mouse clicks. Bring the
    // labels back to the front so click-to-show-About actually fires.
    leftBrandLabel .toFront (false);
    rightBrandLabel.toFront (false);
    refreshABButton();

    setWantsKeyboardFocus (true);
    addKeyListener (this);

    signalChainBar.onBlockClicked = [this] (int id, juce::Rectangle<int> r)
    {
        handleBlockClicked (id, r);
    };

    // Right-click a chain block → open the editor of the plugin loaded in
    // that block's category. If nothing is loaded, do nothing (the EDIT
    // button on the right edge of the chain still owns "add / manage").
    // If multiple plugins occupy that block, pop a tiny chooser. NAM and
    // IR are skipped — they have dedicated panels rather than plugin
    // editors.
    signalChainBar.onBlockRightClicked = [this] (int id, juce::Rectangle<int> r)
    {
        auto& eng = App::get().getAudioEngine();

        // NAM block right-click: open a compact mode / input-calibration
        // popup (Normalize toggle + DI makeup gain + pre-gain). The main amp
        // knobs (input / presence / etc.) already live on the top panel, so
        // this popup is intentionally minimal — mirrors Steve Atkinson's
        // reference Neural Amp Modeler plugin.
        if (id == SignalChainBar::NamAmp)
        {
            auto& nam = eng.getNAM();

            juce::PopupMenu m;
            m.addSectionHeader ("NAM amp");

            // Quick-access for the hosted amp-sim plugin: right-click NAM ->
            // first item opens the plugin editor straight away. No diving.
            if (nam.hasHostedPlugin())
            {
                m.addItem ("Edit plugin: " + nam.getHostedPluginName(),
                           [this] { openHostedNamEditor(); });
                m.addItem ("Clear hosted plugin", [this]
                {
                    App::get().getAudioEngine().getNAM().clearHostedPlugin();
                    signalChainBar.refreshBadges();
                });
                m.addSeparator();
            }

            // Loud warning if any loaded slot was trained at a sample rate
            // different from the current session SR. Running e.g. a 48 kHz
            // model in a 96 kHz session shifts the model's frequency
            // response and aliasing behaviour — tone WILL be off.
            if (nam.hasAnySampleRateMismatch())
            {
                m.addSectionHeader (juce::String::fromUTF8 ("\xE2\x9A\xA0 Model SR mismatch"));
                m.addItem ("Model trained at different SR than session", false, false, [] {});
                m.addItem ("Tone will be inaccurate — resample session or pick a matching model",
                           false, false, [] {});
                m.addSeparator();
            }

            // Output Mode (Raw / Normalized / Calibrated) — same naming as
            // Steve Atkinson's NAM plugin. Radio-style: exactly one ticked.
            const auto mode = nam.getOutputMode();
            using OM = NAMProcessor::OutputMode;
            juce::PopupMenu outMode;
            outMode.addItem ("Raw",        true, mode == OM::Raw,
                             [&nam] { nam.setOutputMode (OM::Raw); });
            outMode.addItem ("Normalized", true, mode == OM::Normalized,
                             [&nam] { nam.setOutputMode (OM::Normalized); });
            outMode.addItem ("Calibrated", true, mode == OM::Calibrated,
                             [&nam] { nam.setOutputMode (OM::Calibrated); });
            m.addSubMenu ("Output Mode", outMode);

            // Oversampling quality — let the user trade CPU for aliasing headroom.
            {
                using OsM = NAMProcessor::OsMode;
                const auto osMode = nam.getOversamplingMode();
                juce::PopupMenu osMenu;
                osMenu.addItem ("Auto (2\u00d7 at \u226488 kHz, off above)", true, osMode == OsM::Auto,
                                [&nam] { nam.setOversamplingMode (OsM::Auto); });
                osMenu.addItem ("Off  \u2014 no oversampling",                true, osMode == OsM::Off,
                                [&nam] { nam.setOversamplingMode (OsM::Off);  });
                osMenu.addItem ("2\u00d7  \u2014 always oversampled",            true, osMode == OsM::x2,
                                [&nam] { nam.setOversamplingMode (OsM::x2);  });
                osMenu.addItem ("4\u00d7 HQ \u2014 maximum quality (~2\u00d7 CPU)", true, osMode == OsM::x4,
                                [&nam] { nam.setOversamplingMode (OsM::x4);  });
                m.addSubMenu ("Oversampling", osMenu);
            }

            m.addItem ("Bypass amp", true, nam.isBypassed(),
                       [&nam] { nam.setBypassed (! nam.isBypassed()); });
            m.addSeparator();

            // DI makeup — applied only when no NAM model is active so the dry
            // monitor sits at a sensible level.
            juce::PopupMenu makeup;
            const float currentMakeup = nam.getDryMakeupDb();
            const float makeupPresets[] = { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f };
            for (float db : makeupPresets)
            {
                const bool tick = std::abs (currentMakeup - db) < 0.05f;
                makeup.addItem ((db == 0.0f ? juce::String ("Off (0 dB)")
                                            : juce::String::formatted ("+%g dB", db)),
                               true, tick,
                               [&nam, db] { nam.setDryMakeupDb (db); });
            }
            m.addSubMenu ("DI makeup (no amp loaded)", makeup);

            // Pre-gain into the NAM model — input calibration for hot/cold DIs.
            juce::PopupMenu pre;
            const float currentPre = nam.getPreGain();
            const float preSteps[] = { -12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f };
            for (float db : preSteps)
            {
                const bool tick = std::abs (currentPre - db) < 0.05f;
                pre.addItem ((db == 0.0f ? juce::String ("0 dB (unity)")
                                         : juce::String::formatted ("%+g dB", db)),
                            true, tick,
                            [&nam, db] { nam.setPreGain (db); });
            }
            m.addSubMenu ("Input calibration", pre);

            m.addSeparator();
            m.addItem ("Open slot manager...", [this]
            {
                handleBlockClicked (SignalChainBar::NamAmp,
                                    signalChainBar.getScreenBounds());
            });

            m.showMenuAsync (juce::PopupMenu::Options()
                               .withTargetScreenArea (r)
                               .withParentComponent (this));
            return;
        }

        PluginChain* chain = nullptr;
        ns::FxCategory cat = ns::FxCategory::Other;
        bool fxCombined = false;
        switch (id)
        {
            case SignalChainBar::Gate:    chain = &eng.getPreFxChain();  cat = ns::FxCategory::Gate;       break;
            case SignalChainBar::Comp:    chain = &eng.getPreFxChain();  cat = ns::FxCategory::Compressor; break;
            case SignalChainBar::Drive:   chain = &eng.getPreFxChain();  cat = ns::FxCategory::Drive;      break;
            case SignalChainBar::Eq:      chain = &eng.getPostFxChain(); cat = ns::FxCategory::EQ;         break;
            case SignalChainBar::Mod:     chain = (signalChainBar.isModBeforeNam() ? &eng.getPreFxChain() : &eng.getPostFxChain()); cat = ns::FxCategory::Modulation; break;
            case SignalChainBar::Delay:   chain = &eng.getPostFxChain(); cat = ns::FxCategory::Delay;      break;
            case SignalChainBar::Reverb:  chain = &eng.getPostFxChain(); cat = ns::FxCategory::Reverb;     break;
            case SignalChainBar::Limiter: chain = &eng.getPostFxChain(); cat = ns::FxCategory::Limiter;    break;
            case SignalChainBar::IrCab:   chain = &eng.getPostFxChain(); cat = ns::FxCategory::IRLoader;   break;
            case SignalChainBar::Fx:      chain = &eng.getPostFxChain(); cat = ns::FxCategory::Other;
                                          fxCombined = true; break;
            default: break;
        }
        if (chain == nullptr) return;
        std::vector<PluginSlot*> matches;
        for (auto* s : chain->getSlotsForUI())
        {
            if (s == nullptr || s->instance == nullptr) continue;
            if (s->category == cat
                || (fxCombined && s->category == ns::FxCategory::Utility))
                matches.push_back (s);
        }
        if (matches.empty()) return;
        if (matches.size() == 1) { CategoryPopup::openPluginEditor (*matches.front()); return; }
        juce::PopupMenu m;
        for (auto* s : matches)
        {
            PluginSlot* sp = s;
            m.addItem (s->displayName, [sp] { CategoryPopup::openPluginEditor (*sp); });
        }
        m.showMenuAsync (juce::PopupMenu::Options()
                           .withTargetScreenArea (r)
                           .withParentComponent (this));
    };

    // Single click on a chain block now toggles bypass for that block.
    signalChainBar.onBlockBypass = [this] (int id)
    {
        auto& eng = App::get().getAudioEngine();
        auto toggleCat = [&] (PluginChain& chain, ns::FxCategory cat)
        {
            chain.setCategoryBypassed (cat, ! chain.isCategoryBypassed (cat));
        };
        switch (id)
        {
            case SignalChainBar::Gate:    toggleCat (eng.getPreFxChain(),  ns::FxCategory::Gate);       break;
            case SignalChainBar::Comp:    toggleCat (eng.getPreFxChain(),  ns::FxCategory::Compressor); break;
            case SignalChainBar::Drive:   toggleCat (eng.getPreFxChain(),  ns::FxCategory::Drive);      break;
            case SignalChainBar::NamAmp:  eng.getNAM().setBypassed (! eng.getNAM().isBypassed());       break;
            case SignalChainBar::IrCab:   toggleCat (eng.getPostFxChain(), ns::FxCategory::IRLoader);   break;
            case SignalChainBar::Eq:      toggleCat (eng.getPostFxChain(), ns::FxCategory::EQ);         break;
            case SignalChainBar::Mod:     toggleCat (eng.getPostFxChain(), ns::FxCategory::Modulation); break;
            case SignalChainBar::Delay:   toggleCat (eng.getPostFxChain(), ns::FxCategory::Delay);      break;
            case SignalChainBar::Reverb:  toggleCat (eng.getPostFxChain(), ns::FxCategory::Reverb);     break;
            case SignalChainBar::Limiter: toggleCat (eng.getPostFxChain(), ns::FxCategory::Limiter);    break;
            case SignalChainBar::Fx:
                // "FX" block buckets Other + Utility, so toggle both together.
                {
                    auto& post = eng.getPostFxChain();
                    const bool now = ! post.isCategoryBypassed (ns::FxCategory::Other);
                    post.setCategoryBypassed (ns::FxCategory::Other,   now);
                    post.setCategoryBypassed (ns::FxCategory::Utility, now);
                }
                break;
            default: break;
        }
        signalChainBar.refreshBadges();
    };

    // Scene bar wiring: left-click recalls, right-click opens management menu.
    sceneBar.onSceneSelected = [this] (int idx)
    {
        auto& sm = App::get().getSceneManager();
        if (sm.hasScene (idx))
        {
            App::get().pushUndoSnapshot();
            sm.recall (idx);
            refreshAllFromEngine();
        }
    };
    sceneBar.onSceneRightClick = [this] (int idx)
    {
        auto& sm = App::get().getSceneManager();
        juce::PopupMenu m;
        const bool has = sm.hasScene (idx);
        m.addItem ("Capture current state", true, false, [this, idx, &sm]
        {
            sm.capture (idx);
            sceneBar.setSceneName (idx, sm.getName (idx));
        });
        m.addItem ("Recall", has, false, [this, idx, &sm]
        {
            App::get().pushUndoSnapshot();
            sm.recall (idx);
            refreshAllFromEngine();
        });
        // Only enabled if a scene exists AND the live state has drifted from it.
        const bool drifted = has && ! sm.currentMatches (idx);
        m.addItem ("Revert to captured", drifted, false, [this, idx, &sm]
        {
            App::get().pushUndoSnapshot();
            sm.recall (idx);
            refreshAllFromEngine();
        });
        m.addItem ("Save current as new (overwrite)", true, false, [this, idx, &sm]
        {
            sm.capture (idx);
            sceneBar.setSceneName (idx, sm.getName (idx));
        });
        m.addItem ("Scene gain offset: " + juce::String (sm.getTrimDb (idx), 1) + " dB...",
                   has, false,
                   [this, idx, &sm]
        {
            const float curTrim = sm.getTrimDb (idx);
            auto* aw = new juce::AlertWindow ("Scene Gain Offset",
                                              "dB (-24 to +24, applies on recall):",
                                              juce::AlertWindow::NoIcon);
            aw->addTextEditor ("trim", juce::String (curTrim, 1));
            aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true,
                juce::ModalCallbackFunction::create ([aw, idx, &sm] (int r)
                {
                    if (r == 1)
                    {
                        const float v = aw->getTextEditorContents ("trim").getFloatValue();
                        sm.setTrimDb (idx, v);
                    }
                    delete aw;
                }), false);
        });
        m.addItem ("Rename...", true, false, [this, idx, &sm]
        {
            auto* aw = new juce::AlertWindow ("Rename Scene", "Scene name:",
                                              juce::AlertWindow::NoIcon);
            aw->addTextEditor ("name", sm.getName (idx));
            aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true,
                juce::ModalCallbackFunction::create ([aw, this, idx, &sm] (int r)
                {
                    if (r == 1)
                    {
                        auto n = aw->getTextEditorContents ("name").trim();
                        if (n.isNotEmpty()) { sm.setName (idx, n); sceneBar.setSceneName (idx, n); }
                    }
                    delete aw;
                }), false);
        });
        m.addItem ("Clear", has, false, [this, idx, &sm]
        {
            sm.clear (idx);
            sceneBar.setSceneName (idx, "SCENE " + juce::String (idx + 1));
        });

        // ---- Recall morph (global setting, shown on every scene) ----
        {
            juce::PopupMenu morphMenu;
            const int curMs = SceneManager::getMorphMs();
            const std::vector<int> opts = { 0, 50, 100, 200, 300, 500 };
            for (size_t k = 0; k < opts.size(); ++k)
            {
                const int v = opts[k];
                morphMenu.addItem (juce::PopupMenu::Item (v == 0 ? "Off (instant)"
                                                                  : juce::String (v) + " ms")
                    .setTicked (curMs == v)
                    .setAction ([v] { SceneManager::setMorphMs (v); }));
            }
            m.addSubMenu ("Recall morph: " + (curMs == 0 ? juce::String ("Off")
                                                          : juce::String (curMs) + " ms"),
                          morphMenu);
        }

        // ---- Footswitch / MIDI ----
        const auto pid = "scene.recall." + juce::String (idx);
        auto& reg = App::get().getMIDILearn();
        juce::String existing;
        for (auto& mp : reg.getMappings())
            if (mp.paramId == pid)
            {
                existing = (mp.isCC() ? "CC " : "Note ") + juce::String (mp.ccOrNote)
                         + (mp.channel == 0 ? " (Omni)" : " Ch " + juce::String (mp.channel));
                break;
            }

        m.addSeparator();
        if (existing.isNotEmpty())
            m.addItem ("Footswitch: " + existing, false, false, [] {});
        if (reg.isLearning() && reg.currentLearnTarget() == pid)
            m.addItem ("Cancel learn", [&reg] { reg.cancelLearn(); });
        else
            m.addItem ("MIDI Learn footswitch (press your switch)",
                       [&reg, pid] { reg.beginLearn (pid); });
        m.addItem ("Clear footswitch mapping", existing.isNotEmpty(), false,
                   [&reg, pid] { reg.clearMapping (pid); });

        m.showMenuAsync ({});
    };

    // Refresh the whole UI whenever a scene is recalled from ANY trigger
    // (UI button, MIDI Program Change, MIDI-learned CC/Note). Without this,
    // MIDI-triggered recalls swap the audio engine state but leave the
    // toolbar / signal-chain bar / NAM badges showing the previously
    // selected scene's state, which makes the visual "stuck on Scene N"
    // even though the audio is correctly on Scene M.
    App::get().getSceneManager().onRecalled =
        [safe = juce::Component::SafePointer<MainComponent> (this)] (int idx)
    {
        juce::MessageManager::callAsync ([safe, idx]
        {
            if (auto* mc = safe.getComponent())
            {
                mc->refreshAllFromEngine();
                mc->setActiveSceneIndicator (idx); // light the correct LED
                App::get().persistLastActiveScene (idx);
            }
        });
    };

    // Refresh the whole SCENE strip when a preset that embeds its own 4-scene
    // bank is loaded -- recall()'s onRecalled only updates the single recalled
    // scene, so this is needed to relabel all four buttons + light the active
    // one for the newly loaded preset.
    App::get().getSceneManager().onBankReloaded =
        [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (auto* mc = safe.getComponent())
                mc->refreshSceneStrip (App::get().readLastActiveScene());
        });
    };

    startTimerHz (10);
}

MainComponent::~MainComponent()
{
    // Detach the scene-recall hook -- App outlives MainComponent on macOS
    // shutdown ordering, so we must NOT leave a dangling lambda capturing
    // a stale `this`.
    App::get().getSceneManager().onRecalled     = nullptr;
    App::get().getSceneManager().onBankReloaded = nullptr;
}

void MainComponent::handleBlockClicked (int blockId, juce::Rectangle<int> screenRect)
{
    auto& eng = App::get().getAudioEngine();

    auto launchPopup = [&] (ns::FxCategory cat, PluginChain& chain, bool showAll = false)
    {
        auto popup = std::make_unique<CategoryPopup> (cat, chain, showAll);
        popup->onChainChanged = [this] { signalChainBar.refreshBadges(); };
        auto& box = juce::CallOutBox::launchAsynchronously (
            std::move (popup), screenRect, this);
        box.setDismissalMouseClicksAreAlwaysConsumed (true);
    };

    switch (blockId)
    {
        case SignalChainBar::Gate:    launchPopup (ns::FxCategory::Gate,       eng.getPreFxChain());  break;
        case SignalChainBar::Comp:    launchPopup (ns::FxCategory::Compressor, eng.getPreFxChain());  break;
        case SignalChainBar::Drive:   launchPopup (ns::FxCategory::Drive,      eng.getPreFxChain());  break;
        case SignalChainBar::Eq:      launchPopup (ns::FxCategory::EQ,         eng.getPostFxChain()); break;
        case SignalChainBar::Mod:     launchPopup (ns::FxCategory::Modulation, eng.getPostFxChain()); break;
        case SignalChainBar::Delay:   launchPopup (ns::FxCategory::Delay,      eng.getPostFxChain()); break;
        case SignalChainBar::Reverb:  launchPopup (ns::FxCategory::Reverb,     eng.getPostFxChain()); break;
        case SignalChainBar::Limiter: launchPopup (ns::FxCategory::Limiter,    eng.getPostFxChain()); break;
        case SignalChainBar::Fx:      launchPopup (ns::FxCategory::Other,      eng.getPostFxChain(), true); break;
        case SignalChainBar::IrCab:   launchPopup (ns::FxCategory::IRLoader,   eng.getPostFxChain()); break;

        case SignalChainBar::NamAmp:
        {
            // The chain-bar NAM button mirrors the 4 NAM slots in the LCD.
            // Click → popup with A/B/C/D, each offering Load / Clear.
            auto& nam = App::get().getAudioEngine().getNAM();
            juce::PopupMenu m;
            for (int i = 0; i < 4; ++i)
            {
                const char letter = (char) ('A' + i);
                const auto name = nam.hasSlot (i) ? nam.getSlotName (i)
                                                  : juce::String ("(empty)");
                juce::PopupMenu sub;
                sub.addItem ("Load model file...", [this, i]
                {
                    chooser = std::make_unique<juce::FileChooser> (
                        "Load NAM model", juce::File{}, "*.nam");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode
                                          | juce::FileBrowserComponent::canSelectFiles,
                        [this, i] (const juce::FileChooser& fc)
                        {
                            auto f = fc.getResult();
                            if (f == juce::File{}) return;
                            juce::String err;
                            if (App::get().getAudioEngine().getNAM().loadSlot (i, f, err))
                                ns::RecentFiles::add ("NAM", f);
                            signalChainBar.refreshBadges();
                        });
                });
                sub.addItem ("Clear", nam.hasSlot (i), false, [&nam, i]
                {
                    nam.clearSlot (i);
                });
                {
                    auto recents = ns::RecentFiles::load ("NAM");
                    if (! recents.isEmpty())
                    {
                        sub.addSeparator();
                        juce::PopupMenu rec;
                        for (auto& f : recents)
                        {
                            const auto path = f.getFullPathName();
                            rec.addItem (f.getFileNameWithoutExtension(),
                                         [this, i, path]
                                         {
                                             juce::String err;
                                             juce::File jf (path);
                                             if (App::get().getAudioEngine().getNAM().loadSlot (i, jf, err))
                                                 ns::RecentFiles::add ("NAM", jf);
                                             signalChainBar.refreshBadges();
                                         });
                        }
                        rec.addSeparator();
                        rec.addItem ("Clear list", [] { ns::RecentFiles::clear ("NAM"); });
                        sub.addSubMenu ("Recent", rec);
                    }
                }
                m.addSubMenu (juce::String::charToString ((juce::juce_wchar) letter)
                              + ": " + name, sub);
            }

            // ---- Hosted amp-sim plugin (mutually exclusive with NAM models)
            m.addSeparator();
            m.addSectionHeader ("Amp-sim plugin");
            if (nam.hasHostedPlugin())
            {
                const auto pName = nam.getHostedPluginName();
                m.addItem ("Edit: " + pName, [this] { openHostedNamEditor(); });
                m.addItem ("Clear hosted plugin", [this]
                {
                    App::get().getAudioEngine().getNAM().clearHostedPlugin();
                    signalChainBar.refreshBadges();
                });
                m.addSubMenu ("Replace with...", buildHostedNamLoadMenu());
            }
            else
            {
                m.addItem ("(no plugin loaded -- NAM models routed)", false, false, [] {});
                m.addSubMenu ("Load amp-sim plugin", buildHostedNamLoadMenu());
            }

            m.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetScreenArea (screenRect)
                                .withParentComponent (this));
            break;
        }
        default: break;
    }
}

//==============================================================================
// Hosted amp-sim plugin helpers (NAM block can load any plugin in addition to
// the 4 NAM model slots; when a plugin is hosted, NAM models are muted in the
// audio path but their state is preserved).
//==============================================================================
juce::PopupMenu MainComponent::buildHostedNamLoadMenu()
{
    juce::PopupMenu out;
    auto& mgr = App::get().getPluginManager();
    auto& known = mgr.getKnownList();

    auto types = known.getTypes();
    if (types.isEmpty())
    {
        out.addItem ("(no plugins known -- run a scan first)", false, false, [] {});
        return out;
    }

    juce::WeakReference<MainComponent> weak (this);

    // ---- (1) Flat alphabetical list, chunked by initial letter so a single
    // sub-menu doesn't blow up to 600 entries on screen. The classifier
    // can mis-bucket amp-sims into Reverb/Delay/etc., so this guarantees
    // every installed plugin is reachable by name.
    {
        std::vector<juce::PluginDescription> all (types.begin(), types.end());
        std::sort (all.begin(), all.end(),
                   [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
                   { return a.name.compareIgnoreCase (b.name) < 0; });

        juce::PopupMenu az;
        juce::PopupMenu bucket;
        juce::juce_wchar currentLetter = 0;
        auto flush = [&]
        {
            if (bucket.getNumItems() > 0)
            {
                juce::String label;
                label += currentLetter;
                az.addSubMenu (label, bucket);
                bucket = juce::PopupMenu();
            }
        };
        for (const auto& d : all)
        {
            auto first = juce::CharacterFunctions::toUpperCase (d.name.isNotEmpty()
                                                                ? d.name[0]
                                                                : (juce::juce_wchar) '#');
            if (! (first >= 'A' && first <= 'Z')) first = '#';
            if (first != currentLetter)
            {
                flush();
                currentLetter = first;
            }
            bucket.addItem (d.name + "  (" + d.pluginFormatName + ")", [weak, d]
            {
                if (auto* self = weak.get()) self->loadHostedNamPlugin (d);
            });
        }
        flush();
        out.addSubMenu ("All plugins (A-Z)", az);
    }

    // ---- (2) Categorized view (best-effort -- the classifier mis-buckets a
    // lot of amp-sims). Kept as a convenience for the obvious cases.
    out.addSeparator();

    struct Cat { const char* label; ns::FxCategory cat; };
    static const Cat order[] = {
        { "Drive / amp-sim style",  ns::FxCategory::Drive       },
        { "Other",                  ns::FxCategory::Other       },
        { "EQ",                     ns::FxCategory::EQ          },
        { "Compressor",             ns::FxCategory::Compressor  },
        { "Modulation",             ns::FxCategory::Modulation  },
        { "Delay",                  ns::FxCategory::Delay       },
        { "Reverb",                 ns::FxCategory::Reverb      },
        { "Limiter",                ns::FxCategory::Limiter     },
        { "Gate",                   ns::FxCategory::Gate        },
        { "IR loader",              ns::FxCategory::IRLoader    },
        { "Utility",                ns::FxCategory::Utility     },
    };

    for (const auto& bk : order)
    {
        juce::PopupMenu sub;
        std::vector<juce::PluginDescription> rows;
        for (const auto& d : types)
            if (ns::classifyPlugin (d) == bk.cat)
                rows.push_back (d);
        std::sort (rows.begin(), rows.end(),
                   [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
                   { return a.name.compareIgnoreCase (b.name) < 0; });
        for (const auto& d : rows)
        {
            sub.addItem (d.name + "  (" + d.pluginFormatName + ")", [weak, d]
            {
                if (auto* self = weak.get()) self->loadHostedNamPlugin (d);
            });
        }
        if (sub.getNumItems() > 0)
            out.addSubMenu (juce::String ("By category: ") + bk.label, sub);
    }
    return out;
}

void MainComponent::loadHostedNamPlugin (const juce::PluginDescription& desc)
{
    auto& eng = App::get().getAudioEngine();
    auto& mgr = App::get().getPluginManager();

    // Crash sentinel: if the plugin crashes the host during instantiation,
    // PendingAdd.txt is on disk and handleCrashedAddOnLaunch will blacklist
    // it on next launch (same protection as CategoryPopup uses).
    mgr.beginGuardedAdd (desc.fileOrIdentifier);

    juce::WeakReference<MainComponent> weak (this);
    const double sr = eng.getCurrentSampleRate();
    const int    bs = eng.getCurrentBlockSize();

    mgr.getFormats().createPluginInstanceAsync (desc, sr, bs,
        [weak, desc] (std::unique_ptr<juce::AudioPluginInstance> inst,
                      const juce::String& err)
        {
            App::get().getPluginManager().endGuardedAdd();
            auto* self = weak.get();
            if (self == nullptr) return;

            if (inst == nullptr)
            {
                const auto pluginName = desc.name;
                const auto reason     = err.isNotEmpty() ? err : juce::String ("Unknown error");
                juce::MessageManager::callAsync ([pluginName, reason]
                {
                    juce::AlertWindow::showAsync (
                        juce::MessageBoxOptions()
                            .withIconType (juce::MessageBoxIconType::WarningIcon)
                            .withTitle ("Plugin could not be loaded")
                            .withMessage ("\"" + pluginName + "\"\n\n" + reason)
                            .withButton ("OK"),
                        nullptr);
                });
                return;
            }

            const auto displayName = desc.name + " (" + desc.pluginFormatName + ")";
            App::get().getAudioEngine().getNAM().setHostedPlugin (std::move (inst),
                                                                  displayName);
            self->signalChainBar.refreshBadges();
        });
}

void MainComponent::openHostedNamEditor()
{
    auto& nam = App::get().getAudioEngine().getNAM();
    auto* inst = nam.getHostedPluginInstance();
    if (inst == nullptr) return;

    if (nam.hostedEditorWindow != nullptr)
    {
        nam.hostedEditorWindow->toFront (true);
        return;
    }

    auto* editor = inst->createEditorIfNeeded();
    if (editor == nullptr) return;

    struct EditorWindow : public juce::DocumentWindow
    {
        EditorWindow (const juce::String& title)
            : juce::DocumentWindow (title,
                                    ns::Colours::background,
                                    juce::DocumentWindow::closeButton) {}
        void closeButtonPressed() override
        {
            setVisible (false);
            clearContentComponent();
            juce::MessageManager::callAsync ([]
            {
                App::get().getAudioEngine().getNAM().hostedEditorWindow.reset();
            });
        }
    };

    auto win = std::make_unique<EditorWindow> (nam.getHostedPluginName());
    win->setUsingNativeTitleBar (true);
    win->setContentOwned (editor, true);
    win->setResizable (editor->isResizable(), false);

    const int w = editor->getWidth();
    const int h = editor->getHeight();
    juce::Component* anchor = nullptr;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
    {
        auto* tw = juce::TopLevelWindow::getTopLevelWindow (i);
        if (tw == nullptr) continue;
        if (dynamic_cast<juce::DialogWindow*> (tw) != nullptr) continue;
        if (dynamic_cast<EditorWindow*>      (tw) != nullptr) continue;
        anchor = tw;
        break;
    }
    if (anchor != nullptr) win->centreAroundComponent (anchor, w, h);
    else                   win->centreWithSize (w, h);

    win->setVisible (true);
    nam.hostedEditorWindow = std::move (win);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &tapBtn && e.mods.isPopupMenu())
        showTapMenu();
    else if (e.eventComponent == &abBtn && e.mods.isPopupMenu())
        showABMenu();
    else if (e.eventComponent == &setupBtn && e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Audio / MIDI Settings...");
        m.addSeparator();
        m.addItem (3, "Noise Gate...");
        m.addSeparator();
        m.addItem (2, "Offline Render Stems...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setupBtn),
            [] (int c)
            {
                if      (c == 1) ns::Dialogs::showAudioMidiSettings();
                else if (c == 2) ns::Dialogs::showOfflineRender();
                else if (c == 3) ns::Dialogs::showNoiseGateDialog();
            });
    }
    else if ((e.eventComponent == &leftBrandLabel || e.eventComponent == &rightBrandLabel)
             && ! e.mods.isPopupMenu())
    {
        ns::Dialogs::showAboutDialog();
    }
    else if (e.eventComponent == &midiBtn && e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Send MIDI Panic (All Notes Off)");
        m.addItem (2, "MIDI Assignments...");
        m.addItem (3, "Footswitch Wizard...");
        m.addSeparator();
        m.addItem (4, "Offline Render...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiBtn),
            [] (int c)
            {
                if      (c == 1) App::get().panic();
                else if (c == 2) ns::Dialogs::showMidiAssignments();
                else if (c == 3) ns::Dialogs::showFootswitchWizard();
                else if (c == 4) ns::Dialogs::showOfflineRender();
            });
    }
}

void MainComponent::showABMenu()
{
    auto& app = App::get();
    const auto curSlot = app.getActiveAB();
    const float lA = app.getLoudnessA();
    const float lB = app.getLoudnessB();
    const float ab = app.getAudioEngine().getOutput().getAbTrimDb();

    auto fmt = [] (float v) {
        return std::isfinite (v) ? juce::String (v, 1) + " LUFS" : juce::String ("--");
    };

    juce::PopupMenu m;
    m.addSectionHeader (juce::String ("A/B Loudness")
                        + "  (A: " + fmt (lA) + "  B: " + fmt (lB) + ")");
    m.addItem (1, juce::String ("Capture loudness for ")
               + (curSlot == App::ABSlot::A ? "A" : "B"));
    m.addItem (2, "Match loudness (apply trim)",
               std::isfinite (lA) && std::isfinite (lB));
    m.addItem (3, "Reset match (trim 0 dB)", std::abs (ab) > 0.01f
                                              || std::isfinite (lA) || std::isfinite (lB));
    m.addSeparator();
    m.addItem (10, "Copy current -> opposite slot");
    m.addItem (11, "Copy A -> B", curSlot == App::ABSlot::B);
    m.addItem (12, "Copy B -> A", curSlot == App::ABSlot::A);
    if (std::abs (ab) > 0.01f)
        m.addSectionHeader ("Current trim: " + juce::String (ab, 1) + " dB");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&abBtn),
        [this] (int choice)
        {
            auto& a = App::get();
            switch (choice)
            {
                case 1:  a.captureABLoudness(); break;
                case 2:  a.matchABLoudness();   break;
                case 3:  a.resetABLoudness();   break;
                case 10:
                    if (a.getActiveAB() == App::ABSlot::A) a.copyAtoB();
                    else                                    a.copyBtoA();
                    break;
                case 11: a.copyAtoB(); break;
                case 12: a.copyBtoA(); break;
                default: break;
            }
            refreshABButton();
        });
}

void MainComponent::showTapMenu()
{
    auto& tc = App::get().getAudioEngine().getTempoClock();
    const double cur = tc.getBpm();

    juce::PopupMenu m;
    m.addSectionHeader ("Tempo: " + juce::String (cur, 1) + " BPM");
    m.addItem (1, "Half  (" + juce::String (cur * 0.5, 1) + ")",  cur * 0.5  >= 30.0);
    m.addItem (2, "Double (" + juce::String (cur * 2.0, 1) + ")", cur * 2.0  <= 300.0);
    m.addSeparator();
    const std::vector<int> presets = { 60, 80, 90, 100, 110, 120, 128, 140, 160, 174 };
    int id = 100;
    for (int p : presets)
        m.addItem (id++, juce::String (p) + " BPM");
    m.addSeparator();
    m.addItem (50, "Type BPM...");

    // ---- MIDI Clock OUT submenu ----
    {
        auto& clk = App::get().getMidiClock();
        juce::PopupMenu clockMenu;
        clockMenu.addItem (200, juce::String ("Send MIDI Clock"),
                           true, clk.isEnabled());
        clockMenu.addSeparator();

        const auto outs = MidiClockSender::availableOutputs();
        const auto current = clk.getOutputName();
        if (outs.isEmpty())
        {
            clockMenu.addItem (-1, "(no MIDI outputs found)", false, false);
        }
        else
        {
            int outId = 300;
            for (auto& nm : outs)
                clockMenu.addItem (outId++, nm, true, nm == current);
        }
        m.addSubMenu ("MIDI Clock OUT", clockMenu);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tapBtn),
        [&tc, cur, presets] (int choice)
        {
            if (choice == 0) return;
            if (choice == 1) { tc.setBpm (cur * 0.5); return; }
            if (choice == 2) { tc.setBpm (cur * 2.0); return; }
            if (choice == 50)
            {
                auto* aw = new juce::AlertWindow ("Set Tempo", "Enter BPM (30-300):",
                                                  juce::AlertWindow::NoIcon);
                aw->addTextEditor ("bpm", juce::String (cur, 1));
                aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true,
                    juce::ModalCallbackFunction::create ([aw, &tc] (int r)
                    {
                        if (r == 1)
                        {
                            const double v = aw->getTextEditorContents ("bpm").getDoubleValue();
                            if (v > 0.0) tc.setBpm (v);
                        }
                        delete aw;
                    }), false);
                return;
            }
            if (choice == 200)
            {
                auto& clk = App::get().getMidiClock();
                clk.setEnabled (! clk.isEnabled());
                return;
            }
            if (choice >= 300 && choice < 400)
            {
                auto& clk  = App::get().getMidiClock();
                const auto outs = MidiClockSender::availableOutputs();
                const int  idx  = choice - 300;
                if (idx >= 0 && idx < outs.size())
                    clk.setOutputByName (outs[idx]);
                return;
            }
            const int idx = choice - 100;
            if (idx >= 0 && idx < (int) presets.size())
                tc.setBpm ((double) presets[(size_t) idx]);
        });
}

void MainComponent::timerCallback()
{
    auto& dm = App::get().getAudioEngine().getDeviceManager();
    int  block = 0;
    double sr  = 0.0;
    if (auto* dev = dm.getCurrentAudioDevice())
    {
        block = dev->getCurrentBufferSizeSamples();
        sr    = dev->getCurrentSampleRate();
    }
    const float cpu = App::get().getAudioEngine().getCpuUsage() * 100.0f;
    const int   latSmp = App::get().getAudioEngine().getTotalLatencySamples();
    const float latMs  = sr > 0.0 ? (float) (latSmp * 1000.0 / sr) : 0.0f;
    juce::String latTxt;
    if (latSmp > 0)
        latTxt = "   |   Lat: " + juce::String (latSmp) + " smp ("
               + juce::String (latMs, 1) + " ms)";
    cpuLabel.setText ("CPU: " + juce::String (cpu, 1) + "%   |   "
                      + juce::String (block) + " smp @ " + juce::String ((int) sr) + " Hz"
                      + latTxt,
                      juce::dontSendNotification);

    // Glitch counter: poll every timer tick (100 ms). If any overruns were
    // detected since the last poll, append a colour-coded badge to the label.
    {
        const int g = App::get().getAudioEngine().getAndResetGlitchCount();
        if (g > 0)
        {
            cpuLabel.setColour (juce::Label::textColourId,
                                g >= 5 ? juce::Colours::orangered
                                       : juce::Colours::yellow);
            cpuLabel.setText (cpuLabel.getText()
                                  + "   |   GLITCH x" + juce::String (g),
                              juce::dontSendNotification);
        }
        else
        {
            cpuLabel.setColour (juce::Label::textColourId, ns::Colours::textSecondary);
        }
    }

    // CPU-spike auto-bypass surfacing: if any slot in either chain is now
    // auto-bypassed, append a brief notice to the CPU label and remember
    // which we've already announced (so we don't spam every 100 ms).
    {
        juce::StringArray newly;
        auto scan = [&] (PluginChain& ch)
        {
            const int n = ch.getNumSlots();
            for (int i = 0; i < n; ++i)
                if (ch.isAutoBypassed (i))
                    if (auto* s = ch.getSlotForUI (i))
                        newly.add (s->displayName);
        };
        scan (App::get().getAudioEngine().getPreFxChain());
        scan (App::get().getAudioEngine().getPostFxChain());
        if (! newly.isEmpty())
            cpuLabel.setText (cpuLabel.getText() + "   |   AUTO-BYPASS: "
                              + newly.joinIntoString (", "),
                              juce::dontSendNotification);
    }

    // Tap-tempo button: live BPM readout. Briefly flash the text colour
    // immediately after a tap.
    {
        auto& tc = App::get().getAudioEngine().getTempoClock();
        tapBtn.setButtonText (juce::String (tc.getBpm(), 1) + " BPM");
        tapBtn.setColour (juce::TextButton::buttonColourId,
                          tc.justTapped() ? ns::Colours::tealAccent
                                          : ns::Colours::chipUnsel);
    }

    // PRESETS button shows the active preset name (or the default label when
    // nothing has been loaded/saved this session), so the user can always see
    // which preset is live.
    {
        const auto pn   = App::get().getPresetManager().getCurrentPresetName();
        const auto want = pn.isNotEmpty() ? pn : juce::String ("PRESETS");
        if (presetsBtn.getButtonText() != want)
            presetsBtn.setButtonText (want);
    }

    signalChainBar.refreshBadges();

    // Undo / Redo enable + lit state.
    {
        const bool cu = App::get().canUndo();
        const bool cr = App::get().canRedo();
        undoBtn.setEnabled (cu);
        redoBtn.setEnabled (cr);
        undoBtn.setColour (juce::TextButton::buttonColourId,
                           cu ? ns::Colours::tealAccent : ns::Colours::chipUnsel);
        redoBtn.setColour (juce::TextButton::buttonColourId,
                           cr ? ns::Colours::tealAccent : ns::Colours::chipUnsel);
    }

    const bool scanning = scanOverlay.refresh();
    if (scanning != scanOverlay.isVisible())
    {
        scanOverlay.setVisible (scanning);
        if (scanning) scanOverlay.toFront (false);
    }

    // Loading overlay: shown ONLY during app startup until all initial
    // state-push batches complete.  After the first dismissal the flag is set
    // and the overlay never appears again, so scene switches are never
    // interrupted by the loading screen.
    if (! startupOverlayDismissed)
    {
        auto& pre  = App::get().getAudioEngine().getPreFxChain();
        auto& post = App::get().getAudioEngine().getPostFxChain();
        const bool loading = pre.isLoading() || post.isLoading();
        const int  active  = pre.getPushesActive()    + post.getPushesActive();
        const int  total   = pre.getPushesScheduled() + post.getPushesScheduled();
        if (loading)
        {
            loadingOverlay.update (active, total);
            loadingOverlay.setVisible (true);
            loadingOverlay.toFront (false);
        }
        else
        {
            startupOverlayDismissed = true;
            loadingOverlay.setVisible (false);
        }
    }
    else if (loadingOverlay.isVisible())
    {
        loadingOverlay.setVisible (false);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (ns::Colours::background);
}

void MainComponent::resized()
{
    using namespace ns::UI;
    const int W = getWidth();
    const int H = getHeight();

    // When the window is shorter than the design height (e.g. Pi 1024×600),
    // scale the top, chain, and bottom section heights proportionally so the
    // centre LCD/rail area gets a fair share of the available pixels.
    // All three panels (AmpKnobsPanel, SignalChainBar, SceneBar) are
    // internally adaptive — they fill whatever bounds they're given.
    const float hScale        = juce::jmin (1.0f, (float) H / (float) kAppHeight);
    const int effectiveTopH   = juce::jmax (90,              juce::roundToInt (kTopH   * hScale));
    const int effectiveChainH = juce::jmax (32,              juce::roundToInt (kChainH * hScale));
    // Minimum must fit: (effectiveBottomH+kSceneBtnH)/2 + 2 + 18(util strip) ≤ effectiveBottomH
    // → effectiveBottomH ≥ kSceneBtnH + 40 = 96.  Using kBottomH as upper bound.
    const int effectiveBottomH= juce::jmax (kSceneBtnH + 40, juce::roundToInt (kBottomH* hScale));

    // Top knobs strip — full width.
    ampKnobs.setBounds (kPad, kPad, W - 2 * kPad, effectiveTopH);

    // Signal chain row: a vertical SCAN button is glued to its left edge and
    // a vertical EDIT button to its right edge. The chain bar itself shrinks
    // to make room for both. Clicking a chain block now toggles bypass; the
    // EDIT button on the right is what opens the load/remove/replace popup.
    const int chainY  = kPad + effectiveTopH + kPad;
    const int vBtnW   = 22;
    const int vBtnGap = 4;
    rescanBtn   .setBounds (kPad,                                       chainY, vBtnW, effectiveChainH);
    chainEditBtn.setBounds (W - kPad - vBtnW,                           chainY, vBtnW, effectiveChainH);
    signalChainBar.setBounds (kPad + vBtnW + vBtnGap,                   chainY,
                              W - 2 * kPad - 2 * (vBtnW + vBtnGap),     effectiveChainH);

    // LCD area + side rails (rails extend all the way down to H - kPad).
    const int lcdY    = chainY + effectiveChainH + kPad;
    const int railBot = H - kPad;
    const int railH   = railBot - lcdY;
    const int meterH  = 28;

    // LCD vertical: leaves room for meter bar then bottom row (scenes).
    const int bottomY = H - kPad - effectiveBottomH;
    const int lcdH    = bottomY - lcdY - kPad - meterH - kPad;

    // Tuner height scales down when the rail is shorter than the design height
    // (e.g. 1024×600 Pi touchscreen, railH ≈ 292 vs design ≈ 632).
    // The proportional formula keeps the knob cells and the tuner all visible.
    // kDesignRailH = kAppHeight - kPad - lcdY = 940 - 16 - 292 = 632.
    constexpr int kDesignRailH = kAppHeight - kPad
                                 - (kPad + kTopH + kPad + kChainH + kPad);
    const int tunerH = juce::jmin (kTunerH,
                                   (int) ((float) railH * kTunerH / kDesignRailH));

    // Tell the left rail how much to reserve at its bottom before setBounds()
    // triggers its resized() — so both knob cells are sized correctly first time.
    sideRail.setReservedTunerHeight (tunerH);

    sideRail .setBounds (kPad,                      lcdY, kLeftRailW,  railH);
    topExtras.setBounds (W - kPad - kRightRailW,    lcdY, kRightRailW, railH);

    const int lcdX = kPad + kLeftRailW + kPad;
    const int lcdW = (W - kPad - kRightRailW - kPad) - lcdX;
    namLcd.setBounds (lcdX, lcdY, lcdW, lcdH);

    // Meter bar matches the LCD width.
    meterBar.setBounds (lcdX, lcdY + lcdH + kPad, lcdW, meterH);

    // Bottom row sits between the rails (rails are below us in z-order).
    sceneBar.setBounds (lcdX, bottomY, lcdW, effectiveBottomH);

    // ---- Items overlaid inside the left rail ----
    const int tunerY = railBot - tunerH;
    tunerPanel.setBounds (kPad, tunerY, kLeftRailW, tunerH);

    // Floating dB readout strip: positioned in the dark gap between the
    // bottom of the AUTO LVL knob's "X.X dB" label (top edge of available
    // space) and the top of the tuner's lavender panel (bottom edge).
    {
        const int knobBottomAbs = sideRail.getY() + sideRail.getAutoLevelBottom();
        const int gapTop  = knobBottomAbs;
        const int gapBot  = tunerY;
        const int gapMidY = (gapTop + gapBot) / 2;
        const int dbH     = 28;
        if (dbReadout != nullptr)
            dbReadout->setBounds (kPad, gapMidY - dbH / 2, kLeftRailW, dbH);
    }

    // ---- NEURAL / STAGE brand text ----
    // Centred horizontally in the gap between each side rail and the nearest
    // scene button. Vertically aligned with the scene-button row.
    const int n           = 4;
    const int totalScenes = n * kSceneBtnW + (n - 1) * kSceneSpacing;
    const int scenesX     = lcdX + (lcdW - totalScenes) / 2;             // scene 1 left
    const int scene4Right = scenesX + totalScenes;                       // scene 4 right

    const int leftRailRight = kPad + kLeftRailW;
    const int rightRailLeft = W - kPad - kRightRailW;

    const int leftGapL = leftRailRight;
    const int leftGapR = scenesX;
    const int rightGapL = scene4Right;
    const int rightGapR = rightRailLeft;

    const int brandH = kSceneBtnH;
    const int brandY = bottomY + (effectiveBottomH - kSceneBtnH) / 2;
    leftBrandLabel .setBounds (leftGapL,  brandY, leftGapR  - leftGapL,  brandH);
    rightBrandLabel.setBounds (rightGapL, brandY, rightGapR - rightGapL, brandH);

    // CPU label centred horizontally between the SCENE 2 and SCENE 3
    // buttons, and vertically between the meter bar bottom and the scene
    // button row.
    {
        const int sceneBtnTopAbs = bottomY + (effectiveBottomH - kSceneBtnH) / 2;
        const int meterBotAbs    = lcdY + lcdH + kPad + meterH;
        const int cpuH           = 14;
        const int cpuY           = (meterBotAbs + sceneBtnTopAbs) / 2 - cpuH / 2;

        const int scene2RightAbs = scenesX + 2 * kSceneBtnW + kSceneSpacing;
        const int scene3LeftAbs  = scenesX + 2 * (kSceneBtnW + kSceneSpacing);
        const int midX           = (scene2RightAbs + scene3LeftAbs) / 2;
        // Generous width so "GLITCH x99" / "AUTO-BYPASS: ..." suffixes are
        // not truncated. Clamp to the rail-to-rail gap so we never overflow
        // the centre cluster.
        const int maxCpuW        = juce::jmax (320, rightGapL - leftGapR - 40);
        const int cpuW           = juce::jmin (640, maxCpuW);

        cpuLabel.setJustificationType (juce::Justification::centred);
        cpuLabel.setBounds (midX - cpuW / 2, cpuY, cpuW, cpuH);
    }

    // (rescanBtn / chainEditBtn are now positioned on the chain row above.)

    // Scan overlay sits across the signal-chain row so it's never covered by
    // the category popup (which anchors below the chain block).
    scanOverlay.setBounds (kPad, chainY, W - 2 * kPad, kChainH);

    // Loading overlay spans the whole component so it blocks all interaction.
    loadingOverlay.setBounds (getLocalBounds());

    // ---- Bottom utility strip ----
    // Place small buttons in the lower gap between the scene buttons and the
    // bottom of the SceneBar area, centred horizontally within the LCD width.
    {
        constexpr int btnH    = 18;
        constexpr int btnGap  = 6;
        const int     btnY    = bottomY + (effectiveBottomH + kSceneBtnH) / 2 + 2;
        // Central utility strip: 7 buttons (PRESETS moved out to the left gap).
        const int     widths[7] = { 64, 30, 56, 56, 52, 86, 48 };
        int totalW = 0;
        for (int w : widths) totalW += w;
        totalW += btnGap * 6;
        int x = lcdX + (lcdW - totalW) / 2;
        juce::TextButton* btns[7] = { &setupBtn, &abBtn, &undoBtn, &redoBtn, &midiBtn, &tapBtn, &specBtn };
        for (int i = 0; i < 7; ++i)
        {
            btns[i]->setBounds (x, btnY, widths[i], btnH);
            x += widths[i] + btnGap;
        }

        // PRESETS dropdown (under NEURAL, left gap -- where LOOPER used to be);
        // LOOPER + BACKING TRACK now share the right gap (under STAGE), each
        // about half width. Clamp both side zones to the central util strip's
        // outer edges so they can never cover it.
        constexpr int sideBtnGap = 6;
        const int utilStripLeft  = lcdX + (lcdW - totalW) / 2;
        const int utilStripRight = utilStripLeft + totalW;

        const int presetsRight = juce::jmin (leftGapR, utilStripLeft - sideBtnGap);
        presetsBtn.setBounds (leftGapL, btnY,
                              juce::jmax (60, presetsRight - leftGapL), btnH);

        const int rightZoneL = juce::jmax (rightGapL, utilStripRight + sideBtnGap);
        const int rightZoneR = rightGapR;
        const int rightZoneW = juce::jmax (120, rightZoneR - rightZoneL);
        constexpr int halfGap = 4;
        const int halfW = (rightZoneW - halfGap) / 2;
        looperBtn .setBounds (rightZoneL, btnY, halfW, btnH);
        backingBtn.setBounds (rightZoneL + halfW + halfGap, btnY,
                              rightZoneR - (rightZoneL + halfW + halfGap), btnH);
    }

    // Spectrum overlay covers the central LCD area when visible.
    if (spectrumOverlay != nullptr)
        spectrumOverlay->setBounds (lcdX, chainY - 4,
                                    lcdW, (bottomY - chainY) + 8);
}

bool MainComponent::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z')
    {
        if (k.getModifiers().isShiftDown()) App::get().redo();
        else                                App::get().undo();
        refreshAllFromEngine();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::escapeKey)
    {
        App::get().panic();
        return true;
    }
    return false;
}

void MainComponent::refreshABButton()
{
    const bool onB = (App::get().getActiveAB() == App::ABSlot::B);
    abBtn.setButtonText (onB ? "B" : "A");
    // Lit when B is active so it's instantly clear which slot you're hearing.
    abBtn.setColour (juce::TextButton::buttonColourId,
                     onB ? ns::Colours::tealAccent : ns::Colours::chipUnsel);
}

void MainComponent::showPresetsMenu()
{
    auto& pm = App::get().getPresetManager();
    const auto files = pm.listPresets();
    const auto cur   = pm.getCurrentPresetFile();

    juce::PopupMenu m;
    if (files.isEmpty())
    {
        m.addItem ("(no presets saved yet)", false, false, [] {});
    }
    else
    {
        for (const auto& f : files)
        {
            const bool tick = (f == cur);
            m.addItem (f.getFileNameWithoutExtension(), true, tick, [this, f]
            {
                App::get().pushUndoSnapshot();
                // Re-arm the startup overlay so the user sees loading progress
                // during a preset switch (plugins may need state-pushes).
                // Scene switches do NOT set this flag, so they stay overlay-free.
                startupOverlayDismissed = false;
                // Show overlay immediately and schedule the actual load via
                // callAsync so the overlay has a chance to paint before the
                // message thread is blocked by cold plugin instantiation.
                loadingOverlay.setVisible (true);
                loadingOverlay.toFront (false);
                juce::MessageManager::callAsync ([this, f]
                {
                    App::get().getPresetManager().load (f);
                    refreshAllFromEngine();
                });
            });
        }
    }

    m.addSeparator();
    m.addItem ("Preset browser...", [] { ns::Dialogs::showPresetBrowser(); });

    // Anchored on the button at the very bottom of the window, so force the
    // menu to open upward where there's room.
    m.showMenuAsync (juce::PopupMenu::Options()
                       .withTargetComponent (&presetsBtn)
                       .withParentComponent (this)
                       .withPreferredPopupDirection (
                           juce::PopupMenu::Options::PopupDirection::upwards));
}

void MainComponent::refreshAllFromEngine()
{
    ampKnobs .refreshFromEngine();
    sideRail .refreshFromEngine();
    topExtras.refreshFromEngine();
    namLcd   .refreshFromEngine();
    refreshABButton();
    repaint();
}

void MainComponent::refreshSceneStrip (int activeIdx)
{
    auto& sm = App::get().getSceneManager();
    for (int i = 0; i < SceneManager::kNumScenes; ++i)
        sceneBar.setSceneName (i, sm.getName (i));
    if (juce::isPositiveAndBelow (activeIdx, SceneManager::kNumScenes))
        sceneBar.setActiveScene (activeIdx);
    refreshAllFromEngine();
}
