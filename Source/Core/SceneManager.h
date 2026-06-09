#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <functional>

class PresetManager;

/** 4 in-memory parameter snapshots tied to the SCENE 1-4 buttons.
 *  Snapshot format = same ValueTree produced by PresetManager.
 */
class SceneManager
{
public:
    static constexpr int kNumScenes = 4;

    explicit SceneManager (PresetManager& pm);

    /** Captures current engine state into the given slot. */
    void capture (int sceneIndex);

    /** Applies the slot to the engine. Returns false if the slot is empty. */
    bool recall  (int sceneIndex);

    bool          hasScene (int sceneIndex) const;
    juce::String  getName  (int sceneIndex) const;
    void          setName  (int sceneIndex, const juce::String& name);
    void          clear    (int sceneIndex);

    /** Per-scene loudness trim (dB). Applied to OutputProcessor's scene-trim
     *  on recall(). Default 0. */
    float         getTrimDb (int sceneIndex) const;
    void          setTrimDb (int sceneIndex, float db);

    /** Recall morph time, ms. 0 = instant (default). When > 0, recall()
     *  briefly fades the output to silence, swaps state, then fades back in
     *  -- a click-free "patch change" the way pro pedalboards do. Clamped
     *  to [0, 500] ms. Stored statically so the UI menu can adjust it. */
    static int    getMorphMs() noexcept;
    static void   setMorphMs (int ms) noexcept;

    /** Compare current engine state to the captured snapshot for the given
     *  scene. Returns true if they're equivalent (i.e. "unchanged since
     *  recall/capture"). Returns false for empty scenes. */
    bool          currentMatches (int sceneIndex) const;

    /** Whole-scene-bank serialisation (folded into preset files). */
    juce::ValueTree toValueTree() const;
    void            fromValueTree (const juce::ValueTree& v);

    /** Raw captured ValueTree for a scene (or invalid tree if empty).
     *  Used by App at boot to walk every scene's chain XML and pre-warm
     *  PluginChain so the first recall of any scene is instant. */
    juce::ValueTree getSceneTree (int sceneIndex) const
    {
        return juce::isPositiveAndBelow (sceneIndex, kNumScenes)
                 ? scenes[(size_t) sceneIndex]
                 : juce::ValueTree();
    }

    /** Fired (on the message thread) every time recall() successfully
     *  swaps in a scene, regardless of trigger (UI button, MIDI PC,
     *  MIDI-learned CC/Note, etc.). The argument is the scene index
     *  just recalled. The UI subscribes to repaint the active-scene
     *  highlight, signal-chain bar, plugin toolbar, etc. -- without
     *  this, MIDI-triggered recalls swap the audio engine but leave
     *  the UI showing the previously-selected scene's state.
     *  Set to nullptr to detach. */
    std::function<void (int /*sceneIndex*/)> onRecalled;

    /** Fired (on the message thread) when an entire scene bank is reloaded
     *  at once -- i.e. when a preset that embeds its own 4 scenes is loaded.
     *  The UI subscribes to refresh all four SCENE button labels and the
     *  active-scene highlight (recall()'s onRecalled only refreshes the one
     *  recalled scene). Set to nullptr to detach. */
    std::function<void()> onBankReloaded;

private:
    PresetManager& presets;
    std::array<juce::ValueTree, kNumScenes> scenes;
    std::array<juce::String,    kNumScenes> names { "SCENE 1", "SCENE 2", "SCENE 3", "SCENE 4" };
    std::array<float,           kNumScenes> trimDb { 0.0f, 0.0f, 0.0f, 0.0f };
};
