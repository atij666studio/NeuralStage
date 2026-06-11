#include "App.h"
#include "UI/Styles/Colours.h"
#include "UI/Styles/UIConstants.h"
#include "UI/Dialogs/ThemedAlerts.h"
#include "Utils/FileUtils.h"
#include "Utils/AppLogger.h"
#include "Utils/CrashSentinel.h"
#include "Audio/NAM/NAMProcessor.h"
#include "BinaryData.h"

App* App::s_instance = nullptr;

static juce::File lastChainFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("LastChain.nschain");
}

static juce::File lastScenesFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("LastScenes.xml");
}

static juce::File lastNamSlotsFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("LastNamSlots.xml");
}

static juce::File lastPreChainFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("LastPreChain.nschain");
}

static juce::File lastTempoFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("LastTempo.txt");
}

static juce::File midiClockSettingsFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("MidiClock.txt");
}

static juce::File morphSettingsFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("SceneMorph.txt");
}

static juce::File windowStateFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("WindowState.txt");
}

static juce::File lastActiveSceneFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("LastActiveScene.txt");
}

static juce::File lastPresetPathFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("LastPresetPath.txt");
}

#ifndef NS_BUILD_PLUGIN
class App::MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (juce::String name)
        : DocumentWindow (name,
                          ns::Colours::background,
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);

       #if JUCE_IOS || JUCE_ANDROID
        setFullScreen (true);
       #else
        setResizable (true, true);
        // Allow the window to be resized below the design size so it is always
        // accessible on small/touchscreen displays. Content will clip rather
        // than the title bar becoming unreachable.
        setResizeLimits (640, 480, 3840, 2400);
        // Restore previous window position+size if we saved one. JUCE's
        // restoreWindowStateFromString clamps to a visible display, so an
        // unplugged external monitor can't strand the window off-screen.
        const auto wf = windowStateFile();
        if (wf.existsAsFile())
        {
            const auto s = wf.loadFileAsString();
            if (s.isNotEmpty() && restoreWindowStateFromString (s))
            {
                // OK -- already positioned/sized.
            }
            else
            {
                centreWithSize (getWidth(), getHeight());
            }
        }
        else
        {
            centreWithSize (getWidth(), getHeight());
        }
        // Clamp to the primary display's usable area so the title bar is always
        // reachable on small screens (e.g. 11.6" 1366x768 touchscreen).
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = d->userArea;
            if (getWidth() > area.getWidth() || getHeight() > area.getHeight())
                centreWithSize (juce::jmin (getWidth(),  area.getWidth()),
                                juce::jmin (getHeight(), area.getHeight()));
        }
       #endif

        setVisible (false);
    }

    void closeButtonPressed() override
    {
        // Persist window state before quitting so the next launch reopens
        // exactly where the user left it.
       #if ! (JUCE_IOS || JUCE_ANDROID)
        windowStateFile().replaceWithText (getWindowStateAsString());
       #endif
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        // Ctrl+Q quit shortcut — useful on touchscreens where the title bar
        // may be partially off-screen and the close button is hard to tap.
        if (key == juce::KeyPress ('q', juce::ModifierKeys::ctrlModifier, 0))
        {
            closeButtonPressed();
            return true;
        }
        return DocumentWindow::keyPressed (key);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
#endif // NS_BUILD_PLUGIN

#ifndef NS_BUILD_PLUGIN
void App::initialise (const juce::String&)
{
    // Show the branded splash screen IMMEDIATELY -- before any heavy init --
    // so there is zero blank-window delay after the exe is clicked. The
    // splash manages its own lifetime and auto-deletes after 2.2 s.
    {
        auto src = juce::ImageCache::getFromMemory (BinaryData::ls_png,
                                                    BinaryData::ls_pngSize);
        if (src.isValid())
        {
            const int srcW    = src.getWidth();
            const int srcH    = src.getHeight();
            const int targetW = juce::jmin (520, srcW / 2);
            const int targetH = juce::roundToInt ((double) srcH * targetW / (double) srcW);
            juce::Image scaled (juce::Image::ARGB, targetW, targetH, true);
            {
                juce::Graphics g (scaled);
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (src, juce::Rectangle<float> (0.0f, 0.0f,
                                                          (float) targetW,
                                                          (float) targetH));
            }
            auto* splash = new juce::SplashScreen ("NeuralStage", scaled, true);
            splash->deleteAfterDelay (juce::RelativeTime::seconds (2.2), false);
        }
    }

    // Make sure our private data directory exists FIRST — the dead-man's-pedal
    // (plugin scan blacklist) and audio-device settings file both live here,
    // and a missing directory is one of the silent failure modes that lets
    // the same broken plugin crash us repeatedly.
    ns::FileUtils::userDataDir().createDirectory();
    ns::FileUtils::documentsDir().createDirectory();
    ns::FileUtils::presetsDir().createDirectory();

    // Install the file logger as early as possible so the rest of init can
    // emit diagnostics. Auto-rotates per date stamp; old logs pruned.
    ns::AppLogger::install ("NeuralStage v" + getApplicationVersion() + " starting on "
                            + juce::SystemStats::getOperatingSystemName());
    juce::Logger::writeToLog ("CPU: "  + juce::SystemStats::getCpuVendor()
                              + "  cores=" + juce::String (juce::SystemStats::getNumCpus())
                              + "  speedMHz=" + juce::String (juce::SystemStats::getCpuSpeedInMegahertz())
                              + "  RAM=" + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB");

    // Crash-recovery sentinel. If the flag is on disk, the previous run did
    // not exit cleanly. We don't change the auto-restore behaviour (chain +
    // scenes are always loaded from the autosave), but we surface the fact
    // in the log and via a non-blocking notice so the user knows the rig
    // was rebuilt from the last 30 s autosave.
    const bool crashedLastRun = ns::CrashSentinel::detectAndArm();
    if (crashedLastRun)
        juce::Logger::writeToLog ("Previous session ended unexpectedly -- restoring last auto-saved chain + scenes.");

    audioEngine.start();

    // Restore audio device settings if present.
    if (auto f = ns::FileUtils::audioDeviceSettingsFile(); f.existsAsFile())
        if (auto xml = juce::parseXML (f))
            audioEngine.getDeviceManager().initialise (1, 2, xml.get(), true);

    presetManager = std::make_unique<PresetManager> (audioEngine, pluginManager);
    sceneManager  = std::make_unique<SceneManager>  (*presetManager);

    // MIDI learn assignments.
    if (auto f = ns::FileUtils::midiAssignmentsFile(); f.existsAsFile())
        if (auto xml = juce::parseXML (f))
            midiLearn.fromValueTree (juce::ValueTree::fromXml (*xml));

    midiManager.setLearnRegistry (&midiLearn);
    midiManager.start (audioEngine.getDeviceManager());

    // Direct Program Change -> scene recall. PC #0 = Scene 1, PC #1 = Scene 2, etc.
    // Bridges the most common pedalboard idiom without requiring a learn step.
    midiManager.onProgramChange = [this] (int pc)
    {
        if (sceneManager == nullptr) return;
        if (pc < 0 || pc >= SceneManager::kNumScenes) return;
        if (! sceneManager->hasScene (pc)) return;
        juce::MessageManager::callAsync ([this, pc]
        {
            pushUndoSnapshot();
            sceneManager->recall (pc);
        });
    };

    // Register scene-recall as MIDI-targetable params (one per scene). These
    // are designed for footswitches: any value >= 0.5 (note-on velocity > 0
    // or CC >= 64) triggers the recall on the message thread.
    for (int i = 0; i < SceneManager::kNumScenes; ++i)
    {
        const auto pid = "scene.recall." + juce::String (i);
        const auto nm  = "Recall Scene " + juce::String (i + 1);
        midiLearn.registerParameter (pid, nm,
            [this, i] (float v01)
            {
                if (v01 >= 0.5f && sceneManager != nullptr)
                {
                    pushUndoSnapshot();
                    sceneManager->recall (i);
                }
            });
    }
    // And a global mute-output target.
    // Toggle on trigger (v01 >= 0.5) so PC footswitches work as latching mute;
    // v01 < 0.5 (CC release) explicitly unmutes for hold-style controllers.
    midiLearn.registerParameter ("output.mute", "Master Mute",
        [this] (float v01)
        {
            if (v01 >= 0.5f)
                audioEngine.getOutput().setMute (! audioEngine.getOutput().isMuted());
            else
                audioEngine.getOutput().setMute (false);
        });
    midiLearn.registerParameter ("input.mute", "Input Mute",
        [this] (float v01)
        {
            audioEngine.getInput().setMute (v01 >= 0.5f);
        });
    // Tap-tempo footswitch: any value >= 0.5 registers a tap.
    midiLearn.registerParameter ("tempo.tap", "Tap Tempo",
        [this] (float v01)
        {
            if (v01 >= 0.5f)
                audioEngine.getTempoClock().tap();
        });

    // ----- Signal-chain block bypass footswitches -----
    // Each takes 0..1; values >= 0.5 bypass the block, < 0.5 enable it.
    // Works equally well from a momentary CC, MIDI note, or program change
    // (use the MIDI Learn dialog to bind whichever your footswitch sends).
    midiLearn.registerParameter ("preFx.bypass", "Pre-FX Bypass",
        [this] (float v01)
        {
            audioEngine.getPreFxChain().setChainBypassed (v01 >= 0.5f);
        });
    midiLearn.registerParameter ("nam.bypass", "NAM Amp Bypass",
        [this] (float v01)
        {
            audioEngine.getNAM().setBypassed (v01 >= 0.5f);
        });
    midiLearn.registerParameter ("postFx.bypass", "Post-FX Bypass",
        [this] (float v01)
        {
            audioEngine.getPostFxChain().setChainBypassed (v01 >= 0.5f);
        });

    // ----- Per-footswitch convenience targets -----
    // A/B compare toggle: any value >= 0.5 flips between the two A/B slots
    // (lets a single momentary footswitch act as an A/B switcher).
    midiLearn.registerParameter ("ab.toggle", "A / B Compare Toggle",
        [this] (float v01)
        {
            if (v01 < 0.5f) return;
            juce::MessageManager::callAsync ([this]
            {
                setActiveAB (activeAB == ABSlot::A ? ABSlot::B : ABSlot::A);
            });
        });
    // Toggle NAM loudness normalization (-18 dBu reference).
    midiLearn.registerParameter ("nam.normalize", "NAM Loudness Normalize",
        [this] (float v01)
        {
            auto& nam = audioEngine.getNAM();
            nam.setNormalized (v01 >= 0.5f);
        });

    mainWindow = std::make_unique<MainWindow> (getApplicationName() + " - Live Rig");

    // (Splash was already created at the top of initialise() for zero-delay
    // appearance; nothing to do here.)

    // Reveal the main window only once the splash has dismissed itself,
    // so we never show an unpainted white app frame behind the splash.
    juce::Timer::callAfterDelay (2200, [this]
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setVisible (true);
            mainWindow->toFront (true);
        }

        // Post-recovery notice -- only if the previous session crashed.
        // Uses the in-app themed overlay (no native window) so dismissing it
        // can never interact with JUCEApplication's quit lifecycle.
        if (ns::CrashSentinel::wasCrashDetected())
        {
            ns::ThemedAlerts::showInfo (
                "Session restored",
                "The previous session ended unexpectedly.\n\n"
                "Your chain, scenes, and NAM slots have been restored from "
                "the most recent auto-save (within 30 seconds of the crash).\n\n"
                "Input is muted on launch -- unmute when you're ready.");
        }
    });

    // SAFETY ON LAUNCH: mute the INPUT so no sound comes through until the
    // user is ready. The input mute button (next to the input knob) lights red
    // so the user has a clear visual cue. The tuner's output-mute button stays
    // dark (output is live once the user unmutes input). Muting input here rather
    // than output keeps the UI state consistent with the indicator buttons.
    audioEngine.getInput().setMute (true);

    // Restore last tempo if persisted.
    if (auto f = lastTempoFile(); f.existsAsFile())
    {
        const auto bpm = f.loadFileAsString().getDoubleValue();
        if (bpm > 0.0)
            audioEngine.getTempoClock().setBpm (bpm);
    }
    midiClock.setBpm (audioEngine.getTempoClock().getBpm());

    // Restore MIDI Clock OUT prefs (format: "<enabled 0|1>\n<device-name-substring>\n").
    if (auto f = midiClockSettingsFile(); f.existsAsFile())
    {
        auto lines = juce::StringArray::fromLines (f.loadFileAsString());
        const bool wantEnabled = lines.size() >= 1 && lines[0].getIntValue() != 0;
        const auto wantDev     = lines.size() >= 2 ? lines[1] : juce::String();
        midiClock.setOutputByName (wantDev);
        midiClock.setEnabled (wantEnabled);
    }

    // Restore scene-recall morph time.
    if (auto f = morphSettingsFile(); f.existsAsFile())
        SceneManager::setMorphMs (f.loadFileAsString().getIntValue());

    // Restore last FX chains (if any). Engine is already prepared by start().
    if (auto f = lastPreChainFile(); f.existsAsFile())
        audioEngine.getPreFxChain().loadFromFile (f,
            pluginManager.getFormats(), pluginManager.getKnownList());

    if (auto f = lastChainFile(); f.existsAsFile())
        audioEngine.getPostFxChain().loadFromFile (f,
            pluginManager.getFormats(), pluginManager.getKnownList());

    // Restore NAM model slots (A-D) so the user's last-loaded captures are
    // back where they left them. Done synchronously: model files are small
    // (~few MB) and loadSlot() is atomic_store inside, so audio keeps running.
    if (auto f = lastNamSlotsFile(); f.existsAsFile())
    {
        if (auto xml = juce::parseXML (f))
        {
            auto& nam = audioEngine.getNAM();
            for (auto* slot : xml->getChildWithTagNameIterator ("Slot"))
            {
                const int idx = slot->getIntAttribute ("index", -1);
                const auto path = slot->getStringAttribute ("file");
                if (idx < 0 || idx >= NAMProcessor::kNumSlots || path.isEmpty()) continue;
                juce::File modelFile (path);
                if (! modelFile.existsAsFile()) continue;
                juce::String err;
                nam.loadSlot (idx, modelFile, err); // best-effort; silent on failure
            }
        }
    }

    // Restore the 4-slot scene bank so the user's captured live-rig scenes
    // survive the relaunch. We only refill the in-memory bank -- we do NOT
    // recall any scene automatically (input is muted on launch and the user
    // should choose which scene to go live with).
    if (auto f = lastScenesFile(); f.existsAsFile())
        if (auto xml = juce::parseXML (f))
            sceneManager->fromValueTree (juce::ValueTree::fromXml (*xml));

    // Safety net: ensure all NAM slots that were loaded successfully at
    // startup have bypass=false and consistent blend weights. The loadSlot()
    // loop above already does this, but this guard covers any edge case where
    // startup order or prior session state left a slot bypassed.
    {
        auto& nam = audioEngine.getNAM();
        for (int i = 0; i < NAMProcessor::kNumSlots; ++i)
            if (nam.hasSlot (i))
                nam.setSlotBypassed (i, false);
        nam.setXYBlend (nam.getXYBlendX(), nam.getXYBlendY());
        // Explicitly snap smoothers to the freshly-computed weights.
        // setXYBlend() no longer calls this internally (to avoid per-event
        // clicks during blend-pad dragging), so the safety net does it.
        nam.triggerSmoothedWeightsReinit();
    }

    // Restore the last-loaded preset name so the PRESETS dropdown shows the
    // correct name on launch (without requiring the user to reload the preset).
    if (auto f = lastPresetPathFile(); f.existsAsFile())
    {
        juce::File presetFile (f.loadFileAsString().trim());
        if (presetFile.existsAsFile())
            presetManager->setLastPresetFile (presetFile);
    }

    // Pre-warm: instantiate every unique plugin referenced across the 4 captured
    // scenes into the off-chain warm pool so the first scene recall is instant.
    {
        auto& preChain  = audioEngine.getPreFxChain();
        auto& postChain = audioEngine.getPostFxChain();
        auto& formats   = pluginManager.getFormats();
        auto& known     = pluginManager.getKnownList();

        // Helper: warm up one preset ValueTree's Pre/Post FX chains.
        auto warmState = [&] (const juce::ValueTree& state)
        {
            const auto preStr  = state.getProperty ("PreFxChainXml" ).toString();
            const auto postStr = state.getProperty ("PostFxChainXml").toString();
            if (preStr.isNotEmpty())
                if (auto x = juce::parseXML (preStr))
                    preChain.warmUpFromXml (*x, formats, known);
            if (postStr.isNotEmpty())
                if (auto x = juce::parseXML (postStr))
                    postChain.warmUpFromXml (*x, formats, known);
        };

        // Warm all 4 saved scenes from the scene bank.
        for (int i = 0; i < SceneManager::kNumScenes; ++i)
        {
            if (! sceneManager->hasScene (i)) continue;
            auto tree = sceneManager->getSceneTree (i);
            if (tree.isValid()) warmState (tree);
        }

    }

    // Restore the visual "active scene" indicator to whatever scene was
    // lit when the user closed the app. The audio state was already loaded
    // from LastChain.nschain above, so this ONLY syncs the SceneBar
    // highlight -- it does NOT trigger a recall.
    juce::MessageManager::callAsync ([this]
    {
        const int lastIdx = readLastActiveScene();
        if (juce::isPositiveAndBelow (lastIdx, SceneManager::kNumScenes))
        {
            if (auto* mc = dynamic_cast<MainComponent*> (
                    mainWindow != nullptr ? mainWindow->getContentComponent() : nullptr))
                mc->setActiveSceneIndicator (lastIdx);
        }
    });

    // Seed A/B with the freshly loaded state.
    snapshotA = presetManager->captureState();
    snapshotB = snapshotA.createCopy();

    // We deliberately do NOT auto-scan on launch. Plugin scanning only
    // runs when the user clicks Rescan. If a previous run left the
    // scan-in-progress sentinel on disk, just clean it up so we don't
    // mislead anything else that checks it.
    {
        auto flag = ns::FileUtils::userDataDir().getChildFile ("ScanInProgress.flag");
        if (flag.existsAsFile()) flag.deleteFile();
    }

    // Auto-save chain state every 30 s so a crash mid-session can recover
    // (shutdown() does the final save, but it doesn't run on a hard crash).
    startTimer (30000);

    // Deferred NAM reinit: fires 1 second after all startup code completes.
    // Belt-and-suspenders in case plugin warm-up or an audio-device event
    // triggered a prepare() call that reset the smoothers after the safety
    // net above ran.  The self-healing in process() covers block-by-block
    // races; this covers any gap between the last prepare() and audio start.
    juce::Timer::callAfterDelay (1000, [this]
    {
        auto& nam = audioEngine.getNAM();
        for (int i = 0; i < NAMProcessor::kNumSlots; ++i)
            if (nam.hasSlot (i))
                nam.setSlotBypassed (i, false);
        nam.setXYBlend (nam.getXYBlendX(), nam.getXYBlendY());
        nam.triggerSmoothedWeightsReinit();
    });
}
#endif // NS_BUILD_PLUGIN

void App::timerCallback()
{
    audioEngine.getPreFxChain ().saveToFile (lastPreChainFile());
    audioEngine.getPostFxChain().saveToFile (lastChainFile());
    lastTempoFile().replaceWithText (juce::String (audioEngine.getTempoClock().getBpm(), 2));

    // Keep MIDI clock following the tempo source.
    midiClock.setBpm (audioEngine.getTempoClock().getBpm());

    midiClockSettingsFile().replaceWithText (juce::String (midiClock.isEnabled() ? 1 : 0)
                                              + "\n" + midiClock.getOutputName() + "\n");

    morphSettingsFile().replaceWithText (juce::String (SceneManager::getMorphMs()));

    // Crash-recovery autosave for scene bank + NAM slot file paths.
    if (sceneManager != nullptr)
        if (auto xml = sceneManager->toValueTree().createXml())
            xml->writeTo (lastScenesFile());

    // Persist last-loaded preset path so the PRESETS dropdown name survives a crash.
    if (presetManager != nullptr)
    {
        auto pf = presetManager->getCurrentPresetFile();
        if (pf != juce::File())
            lastPresetPathFile().replaceWithText (pf.getFullPathName());
    }

    {
        juce::XmlElement xml ("NamSlots");
        auto& nam = audioEngine.getNAM();
        for (int i = 0; i < NAMProcessor::kNumSlots; ++i)
        {
            if (! nam.hasSlot (i)) continue;
            auto f = nam.getSlotFile (i);
            if (f == juce::File{}) continue;
            auto* s = xml.createNewChildElement ("Slot");
            s->setAttribute ("index", i);
            s->setAttribute ("file",  f.getFullPathName());
        }
        xml.writeTo (lastNamSlotsFile());
    }
}

#ifndef NS_BUILD_PLUGIN
void App::shutdown()
{
    stopTimer();
    midiClock.setEnabled (false);
    audioEngine.getPreFxChain ().saveToFile (lastPreChainFile());
    audioEngine.getPostFxChain().saveToFile (lastChainFile());
    lastTempoFile().replaceWithText (juce::String (audioEngine.getTempoClock().getBpm(), 2));

    // Persist window state in case the user quit via cmd-Q (which bypasses
    // closeButtonPressed on macOS).
   #if ! (JUCE_IOS || JUCE_ANDROID)
    if (mainWindow != nullptr)
        windowStateFile().replaceWithText (mainWindow->getWindowStateAsString());
   #endif

    // Persist audio device settings + MIDI assignments.
    if (auto xml = audioEngine.getDeviceManager().createStateXml())
        xml->writeTo (ns::FileUtils::audioDeviceSettingsFile());
    if (auto xml = midiLearn.toValueTree().createXml())
        xml->writeTo (ns::FileUtils::midiAssignmentsFile());

    // Persist the scene bank and NAM slot file list so the next launch
    // restores them. Mirrors what the 30 s autosave timer writes.
    if (sceneManager != nullptr)
        if (auto xml = sceneManager->toValueTree().createXml())
            xml->writeTo (lastScenesFile());

    // Persist last-loaded preset path for name display on next launch.
    if (presetManager != nullptr)
    {
        auto pf = presetManager->getCurrentPresetFile();
        if (pf != juce::File())
            lastPresetPathFile().replaceWithText (pf.getFullPathName());
    }
    {
        juce::XmlElement xml ("NamSlots");
        auto& nam = audioEngine.getNAM();
        for (int i = 0; i < NAMProcessor::kNumSlots; ++i)
        {
            if (! nam.hasSlot (i)) continue;
            auto f = nam.getSlotFile (i);
            if (f == juce::File{}) continue;
            auto* s = xml.createNewChildElement ("Slot");
            s->setAttribute ("index", i);
            s->setAttribute ("file",  f.getFullPathName());
        }
        xml.writeTo (lastNamSlotsFile());
    }

    midiManager.stop (audioEngine.getDeviceManager());

    mainWindow.reset();
    audioEngine.stop();

    // Clean exit — disarm the crash sentinel and detach the logger.
    ns::CrashSentinel::disarm();
    juce::Logger::writeToLog ("NeuralStage shutdown complete.");
    ns::AppLogger::uninstall();
}
#endif // NS_BUILD_PLUGIN

//==============================================================================
// Plugin-mode initialise / shutdown
//==============================================================================
#ifdef NS_BUILD_PLUGIN
void App::pluginInitialise()
{
    ns::FileUtils::userDataDir().createDirectory();
    ns::FileUtils::documentsDir().createDirectory();
    ns::FileUtils::presetsDir().createDirectory();

    ns::AppLogger::install ("NeuralStage v" + getApplicationVersion() + " (plugin) starting on "
                            + juce::SystemStats::getOperatingSystemName());
    juce::Logger::writeToLog ("CPU: "  + juce::SystemStats::getCpuVendor()
                              + "  cores=" + juce::String (juce::SystemStats::getNumCpus())
                              + "  RAM=" + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB");

    ns::CrashSentinel::detectAndArm();

    presetManager = std::make_unique<PresetManager> (audioEngine, pluginManager);
    sceneManager  = std::make_unique<SceneManager>  (*presetManager);

    if (auto f = ns::FileUtils::midiAssignmentsFile(); f.existsAsFile())
        if (auto xml = juce::parseXML (f))
            midiLearn.fromValueTree (juce::ValueTree::fromXml (*xml));

    midiManager.setLearnRegistry (&midiLearn);

    // Register the same MIDI-targetable parameters as standalone so footswitch
    // assignments made in the standalone app carry across to the plugin.
    midiManager.onProgramChange = [this] (int pc)
    {
        if (sceneManager == nullptr) return;
        if (pc < 0 || pc >= SceneManager::kNumScenes) return;
        if (! sceneManager->hasScene (pc)) return;
        juce::MessageManager::callAsync ([this, pc]
        {
            pushUndoSnapshot();
            sceneManager->recall (pc);
        });
    };

    for (int i = 0; i < SceneManager::kNumScenes; ++i)
    {
        const auto pid = "scene.recall." + juce::String (i);
        const auto nm  = "Recall Scene "  + juce::String (i + 1);
        midiLearn.registerParameter (pid, nm,
            [this, i] (float v01)
            {
                if (v01 >= 0.5f && sceneManager != nullptr)
                {
                    pushUndoSnapshot();
                    sceneManager->recall (i);
                }
            });
    }
    midiLearn.registerParameter ("output.mute", "Master Mute",
        [this] (float v01)
        {
            if (v01 >= 0.5f)
                audioEngine.getOutput().setMute (! audioEngine.getOutput().isMuted());
            else
                audioEngine.getOutput().setMute (false);
        });
    midiLearn.registerParameter ("input.mute", "Input Mute",
        [this] (float v01) { audioEngine.getInput().setMute (v01 >= 0.5f); });
    midiLearn.registerParameter ("tempo.tap", "Tap Tempo",
        [this] (float v01) { if (v01 >= 0.5f) audioEngine.getTempoClock().tap(); });
    midiLearn.registerParameter ("preFx.bypass", "Pre-FX Bypass",
        [this] (float v01) { audioEngine.getPreFxChain().setChainBypassed (v01 >= 0.5f); });
    midiLearn.registerParameter ("nam.bypass", "NAM Amp Bypass",
        [this] (float v01) { audioEngine.getNAM().setBypassed (v01 >= 0.5f); });
    midiLearn.registerParameter ("postFx.bypass", "Post-FX Bypass",
        [this] (float v01) { audioEngine.getPostFxChain().setChainBypassed (v01 >= 0.5f); });
    midiLearn.registerParameter ("ab.toggle", "A / B Compare Toggle",
        [this] (float v01)
        {
            if (v01 < 0.5f) return;
            juce::MessageManager::callAsync ([this]
            { setActiveAB (activeAB == ABSlot::A ? ABSlot::B : ABSlot::A); });
        });
    midiLearn.registerParameter ("nam.normalize", "NAM Loudness Normalize",
        [this] (float v01) { audioEngine.getNAM().setNormalized (v01 >= 0.5f); });

    // In plugin mode we do NOT restore chain files or NAM model slots here:
    //  • Chain state: the DAW owns it (setStateInformation) — loading chain
    //    files would re-entrantly instantiate hosted VST3 plugins while the
    //    host holds plugin-loading locks → deadlock / crash in Reaper.
    //  • NAM slots: loading large model files synchronously from prepareToPlay
    //    (audio thread) would cause an audio dropout of several seconds.
    //    State comes from setStateInformation → PresetManager::restoreState.
    //  • Do NOT call setMute(true) here — unlike the standalone boot (where
    //    we mute while loading the chain), the plugin must pass audio
    //    immediately.  The mute is a standalone-only boot gate.

    if (auto f = lastTempoFile(); f.existsAsFile())
    {
        const auto bpm = f.loadFileAsString().getDoubleValue();
        if (bpm > 0.0) audioEngine.getTempoClock().setBpm (bpm);
    }
    midiClock.setBpm (audioEngine.getTempoClock().getBpm());

    if (auto f = morphSettingsFile(); f.existsAsFile())
        SceneManager::setMorphMs (f.loadFileAsString().getIntValue());

    // Scene bank metadata (safe – pure XML, no plugin loading).
    if (auto f = lastScenesFile(); f.existsAsFile())
        if (auto xml = juce::parseXML (f))
            sceneManager->fromValueTree (juce::ValueTree::fromXml (*xml));

    snapshotA = presetManager->captureState();
    snapshotB = snapshotA.createCopy();

    // startTimer must run on the message thread.  Use callAsync so this is
    // safe even when pluginInitialise() is called from the audio thread.
    juce::MessageManager::callAsync ([this] { startTimer (30000); });
}

void App::pluginWarmUp()
{
    // Must be called on the message thread (scene plugin instantiation is not
    // thread-safe off-thread). Typically fired via callAsync from
    // setStateInformation — after the DAW has released its plugin-loading locks.
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (sceneManager == nullptr) return;

    auto& preChain  = audioEngine.getPreFxChain();
    auto& postChain = audioEngine.getPostFxChain();
    auto& formats   = pluginManager.getFormats();
    auto& known     = pluginManager.getKnownList();
    for (int i = 0; i < SceneManager::kNumScenes; ++i)
    {
        if (! sceneManager->hasScene (i)) continue;
        auto tree = sceneManager->getSceneTree (i);
        if (! tree.isValid()) continue;
        const auto preStr  = tree.getProperty ("PreFxChainXml" ).toString();
        const auto postStr = tree.getProperty ("PostFxChainXml").toString();
        if (preStr.isNotEmpty())
            if (auto x = juce::parseXML (preStr))
                preChain.warmUpFromXml (*x, formats, known);
        if (postStr.isNotEmpty())
            if (auto x = juce::parseXML (postStr))
                postChain.warmUpFromXml (*x, formats, known);
    }
}

void App::pluginShutdown()
{
    stopTimer();
    midiClock.setEnabled (false);

    audioEngine.getPreFxChain ().saveToFile (lastPreChainFile());
    audioEngine.getPostFxChain().saveToFile (lastChainFile());
    lastTempoFile().replaceWithText (juce::String (audioEngine.getTempoClock().getBpm(), 2));

    if (auto xml = midiLearn.toValueTree().createXml())
        xml->writeTo (ns::FileUtils::midiAssignmentsFile());

    if (sceneManager != nullptr)
        if (auto xml = sceneManager->toValueTree().createXml())
            xml->writeTo (lastScenesFile());

    {
        juce::XmlElement xml ("NamSlots");
        auto& nam = audioEngine.getNAM();
        for (int i = 0; i < NAMProcessor::kNumSlots; ++i)
        {
            if (! nam.hasSlot (i)) continue;
            auto f = nam.getSlotFile (i);
            if (f == juce::File{}) continue;
            auto* s = xml.createNewChildElement ("Slot");
            s->setAttribute ("index", i);
            s->setAttribute ("file",  f.getFullPathName());
        }
        xml.writeTo (lastNamSlotsFile());
    }

    ns::CrashSentinel::disarm();
    juce::Logger::writeToLog ("NeuralStage plugin shutdown complete.");
    ns::AppLogger::uninstall();
}
#endif // NS_BUILD_PLUGIN

void App::pushUndoSnapshot()
{
    if (presetManager == nullptr) return;
    undoStack.push_back (presetManager->captureState());
    if (undoStack.size() > kMaxUndo) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void App::persistLastActiveScene (int idx)
{
    if (! juce::isPositiveAndBelow (idx, SceneManager::kNumScenes)) return;
    auto f = lastActiveSceneFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::String (idx));
}

int App::readLastActiveScene() const
{
    auto f = lastActiveSceneFile();
    if (! f.existsAsFile()) return -1;
    const int v = f.loadFileAsString().trim().getIntValue();
    return juce::isPositiveAndBelow (v, SceneManager::kNumScenes) ? v : -1;
}

void App::undo()
{
    if (undoStack.empty() || presetManager == nullptr) return;
    redoStack.push_back (presetManager->captureState());
    auto prev = undoStack.back(); undoStack.pop_back();
    presetManager->restoreState (prev);
}

void App::redo()
{
    if (redoStack.empty() || presetManager == nullptr) return;
    undoStack.push_back (presetManager->captureState());
    auto next = redoStack.back(); redoStack.pop_back();
    presetManager->restoreState (next);
}

void App::panic()
{
    audioEngine.getPreFxChain ().requestPanic();
    audioEngine.getPostFxChain().requestPanic();
    midiClock.sendPanic();

    // Also clear the looper to silence any recorded tail.
    audioEngine.getLooper().tapClear();
}

void App::setActiveAB (ABSlot s)
{
    if (presetManager == nullptr || s == activeAB) return;
    // Save current into the slot we're leaving, then load the new slot.
    auto cur = presetManager->captureState();
    if (activeAB == ABSlot::A) snapshotA = cur; else snapshotB = cur;
    activeAB = s;
    auto& target = (activeAB == ABSlot::A) ? snapshotA : snapshotB;
    if (target.isValid() && ! target.isEquivalentTo (cur))
        presetManager->restoreState (target);
}

void App::copyAtoB() { snapshotB = (activeAB == ABSlot::A ? presetManager->captureState() : snapshotA).createCopy(); }
void App::copyBtoA() { snapshotA = (activeAB == ABSlot::B ? presetManager->captureState() : snapshotB).createCopy(); }

void App::captureABLoudness()
{
    const float l = audioEngine.getOutput().getOutputLoudnessDb();
    (activeAB == ABSlot::A ? loudA : loudB) = l;
}

float App::matchABLoudness()
{
    if (! std::isfinite (loudA) || ! std::isfinite (loudB))
        return 0.0f;
    // Match the *inactive* slot to the active one by adjusting ab-trim.
    // Convention: while on B, "match" makes B as loud as A (so listening to
    // B you hear A's loudness with B's tone).
    const float ref     = (activeAB == ABSlot::A) ? loudA : loudB;
    const float current = (activeAB == ABSlot::A) ? loudA : loudB; // active
    const float other   = (activeAB == ABSlot::A) ? loudB : loudA;
    if (! std::isfinite (other) || ! std::isfinite (ref))
        return 0.0f;
    juce::ignoreUnused (current);
    // Apply the trim NOW so the active slot matches the reference of the other.
    // Simpler interpretation: bring active to match the *opposite* slot's loudness.
    const float delta = (activeAB == ABSlot::A) ? (loudB - loudA) : (loudA - loudB);
    audioEngine.getOutput().setAbTrimDb (delta);
    return delta;
}

void App::resetABLoudness()
{
    audioEngine.getOutput().setAbTrimDb (0.0f);
    loudA = loudB = std::numeric_limits<float>::quiet_NaN();
}

App::App()
{
    s_instance = this;
}

App::~App()
{
    s_instance = nullptr;
}
