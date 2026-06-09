#pragma once
#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;
class PluginManager;

/** Centralised state snapshot → used by Presets, Scenes, A/B compare and Undo.
 *
 *  Snapshot captures every parameter the engine exposes plus the plugin
 *  chain XML.  Apply re-instantiates plugins via the PluginManager.
 *
 *  Preset files live in ~/Documents/NeuralStage/Presets/*.nspreset (XML).
 */
class PresetManager
{
public:
    PresetManager (AudioEngine& eng, PluginManager& mgr);

    // ---- snapshot / restore ----
    juce::ValueTree captureState() const;
    void            restoreState (const juce::ValueTree& state);

    // ---- file I/O ----
    bool save (const juce::File& file) const;
    bool load (const juce::File& file);

    juce::File              presetsDirectory() const;
    juce::Array<juce::File> listPresets() const;

    AudioEngine& getEngine() noexcept { return engine; }

    /** The preset file most recently loaded or saved this session (invalid
     *  File if none yet). Used by the bottom-bar PRESETS dropdown to show
     *  the active preset name. */
    juce::File   getCurrentPresetFile() const { return currentPresetFile; }
    juce::String getCurrentPresetName() const { return currentPresetFile.getFileNameWithoutExtension(); }

    /** Restores the last-preset file path from persisted storage so the
     *  dropdown name is correct on launch without requiring the user to
     *  reload the preset. Does NOT trigger any state restore. */
    void setLastPresetFile (const juce::File& f) { currentPresetFile = f; }

    //==========================================================================
    // Tags / category metadata
    //
    // Each preset stores a free-form comma-separated tag list and a single
    // category string at the XML root. The session caches the values for the
    // *last loaded/saved* preset so a re-save preserves them and the browser
    // can edit them without re-parsing the file. Empty category means
    // "Uncategorised".
    void              setActiveCategory (const juce::String& c) { activeCategory = c.trim(); }
    void              setActiveTags     (const juce::String& t) { activeTags     = t.trim(); }
    const juce::String& getActiveCategory() const noexcept       { return activeCategory; }
    const juce::String& getActiveTags()     const noexcept       { return activeTags; }

    /** Light-weight metadata read — does NOT touch engine state.
     *  Returns empty strings if the file is missing/unreadable. */
    static juce::String readCategoryFromFile (const juce::File& f);
    static juce::String readTagsFromFile     (const juce::File& f);

    /** Edit metadata on an on-disk preset without doing a full load/restore
     *  cycle (which would replace the live engine state). Returns false if
     *  the file can't be parsed. */
    static bool writeMetaToFile (const juce::File& f,
                                 const juce::String& category,
                                 const juce::String& tags);

private:
    AudioEngine&   engine;
    PluginManager& plugins;
    juce::String   activeCategory;
    juce::String   activeTags;
    mutable juce::File currentPresetFile;

    // Cache of the chain-XML last successfully applied to each plugin chain,
    // tagged with the chain's mutationGen at the time of caching. Lets
    // restoreChain() short-circuit identical recalls without paying the cost
    // of re-serialising the live chain via saveStateToXml() every time —
    // that walk takes tens of ms per recall once a few plugins are loaded
    // and is exactly what made scene switching feel laggy.
    //
    // ALSO used by captureState(): when the user clicks scene 2 the host
    // first pushes an undo snapshot of the live state, which previously
    // called saveStateToXml() on both chains -- serialising every hosted
    // plugin's state byte-blob -- BEFORE the recall even started. That was
    // the visible "not instant" lag the user complained about. We now
    // reuse the cached XML when the chain has not mutated since the last
    // restore (which is the common path during live scene-switching).
    //
    // CAVEAT: parameter tweaks INSIDE a hosted plugin's own UI do not bump
    // mutationGen, so cached XML may be stale relative to a plugin's
    // current internal state. In the scene-switching workflow the user is
    // not also tweaking plugin GUIs simultaneously, so this is acceptable.
    // Use captureStateFresh() if a guaranteed-current capture is required
    // (e.g. file-save).
    mutable juce::String lastPreFxXml,  lastPostFxXml;
    mutable int          lastPreFxGen  { -1 };
    mutable int          lastPostFxGen { -1 };

public:
    /** Force-refresh of the chain-XML cache. Call after the user has been
     *  editing a hosted plugin's GUI and we are about to do an operation
     *  that must capture the live byte-accurate state (file save, A/B
     *  copy, etc.). Cheap when caches are already up-to-date. */
    void invalidateChainCache() noexcept
    {
        lastPreFxXml  = {}; lastPreFxGen  = -1;
        lastPostFxXml = {}; lastPostFxGen = -1;
    }
};
