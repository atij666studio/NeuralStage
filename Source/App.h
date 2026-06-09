#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include "UI/MainComponent.h"
#include "Audio/AudioEngine.h"
#include "PluginHost/PluginManager.h"
#include "Core/PresetManager.h"
#include "Core/SceneManager.h"
#include "MIDI/MIDIManager.h"
#include "MIDI/MIDILearn.h"
#include "MIDI/MidiClockSender.h"

/** NeuralStage application state / service locator.
 *
 *  In standalone builds (juce_add_gui_app / juce_add_plugin Standalone format)
 *  this class also extends JUCEApplication and owns the main window.
 *
 *  In plugin builds (NS_BUILD_PLUGIN=1, compiled by juce_add_plugin for
 *  VST3 / CLAP) it is a plain singleton created by NeuralStageProcessor.
 *  App::get() works identically in both modes via the s_instance pointer. */
class App
#ifndef NS_BUILD_PLUGIN
    : public juce::JUCEApplication
    ,
#else
    :
#endif
      private juce::Timer
{
public:
    App();

#ifndef NS_BUILD_PLUGIN
    ~App() override;

    const juce::String getApplicationName() override    { return "NeuralStage"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override           { return true; }

    void initialise (const juce::String&) override;
    void shutdown() override;
    void systemRequestedQuit() override { quit(); }
#else
    ~App();

    const juce::String getApplicationName()    const noexcept { return "NeuralStage"; }
    const juce::String getApplicationVersion() const noexcept { return "0.2.0"; }

    /** Called by NeuralStageProcessor constructor. Sets up state,
     *  managers, MIDI, file restore — everything except audio device
     *  setup (that happens in prepareToPlay) and window creation. */
    void pluginInitialise();

    /** Called by NeuralStageProcessor destructor. Saves state, stops MIDI. */
    void pluginShutdown();

    /** Pre-instantiate all scene plugins into the warm pool so scene switches
     *  are instant. Called via callAsync from setStateInformation, AFTER the
     *  DAW releases its plugin-loading locks. Must be called on the message thread. */
    void pluginWarmUp();
#endif

    // ---- Global accessor (works in both standalone and plugin builds) ----
    static App& get() noexcept { jassert (s_instance != nullptr); return *s_instance; }

    AudioEngine&        getAudioEngine()    noexcept { return audioEngine;   }
    PluginManager&      getPluginManager()  noexcept { return pluginManager; }
    PresetManager&      getPresetManager()  noexcept { return *presetManager; }
    SceneManager&       getSceneManager()   noexcept { return *sceneManager;  }
    MIDIManager&        getMIDIManager()    noexcept { return midiManager;   }
    MIDILearnRegistry&  getMIDILearn()      noexcept { return midiLearn;     }
    MidiClockSender&    getMidiClock()      noexcept { return midiClock;     }
    juce::UndoManager&  getUndoManager()    noexcept { return undoManager;   }

    /** Push a snapshot of the current state to the undo stack.
     *  Call BEFORE making a change that should be undoable. */
    void pushUndoSnapshot();
    bool canUndo() const noexcept { return undoStack.size() > 0; }
    bool canRedo() const noexcept { return redoStack.size() > 0; }
    void undo();
    void redo();

    /** All-Sound-Off / All-Notes-Off: injects MIDI panic into both plugin
     *  chains AND the external MIDI Clock output (if open). Live-rig safety. */
    void panic();

    // ---- A/B compare ----
    enum class ABSlot { A = 0, B = 1 };
    ABSlot getActiveAB() const noexcept { return activeAB; }
    void   setActiveAB (ABSlot s);
    void   copyAtoB();
    void   copyBtoA();

    /** Capture current short-term loudness reading for the active slot. */
    void   captureABLoudness();
    /** Apply auto-trim so B matches A (or vice versa). Returns the dB delta
     *  that was applied (0 if a captured loudness is missing). */
    float  matchABLoudness();
    /** Reset the loudness-match trim (sets engine ab-trim to 0). */
    void   resetABLoudness();

    float  getLoudnessA() const noexcept { return loudA; }
    float  getLoudnessB() const noexcept { return loudB; }

    /** Persist / read the last-active scene index (0..3) across launches.
     *  Stored in userDataDir/LastActiveScene.txt. Returns -1 if absent.
     *  Persistence is "best effort" -- safe to call from any thread. */
    void persistLastActiveScene (int idx);
    int  readLastActiveScene() const;

private:
    void timerCallback() override;

    static App* s_instance;

    AudioEngine                  audioEngine;
    PluginManager                pluginManager;
    std::unique_ptr<PresetManager> presetManager;
    std::unique_ptr<SceneManager>  sceneManager;
    MIDIManager                  midiManager;
    MIDILearnRegistry            midiLearn;
    MidiClockSender              midiClock;
    juce::UndoManager            undoManager;

    std::vector<juce::ValueTree> undoStack;
    std::vector<juce::ValueTree> redoStack;
    static constexpr size_t       kMaxUndo { 64 };

    juce::ValueTree              snapshotA, snapshotB;
    ABSlot                       activeAB { ABSlot::A };

    // Captured short-term loudness (dBFS) for each slot, NaN until captured.
    float                        loudA { std::numeric_limits<float>::quiet_NaN() };
    float                        loudB { std::numeric_limits<float>::quiet_NaN() };

#ifndef NS_BUILD_PLUGIN
    class MainWindow;
    std::unique_ptr<MainWindow>  mainWindow;
#endif
};
