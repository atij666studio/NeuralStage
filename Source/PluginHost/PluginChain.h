#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginSlot.h"
#include <vector>
#include <memory>
#include <mutex>
#include <array>
#include <algorithm>

/** Real-time-safe chain of audio plugins.
 *  - Mutates (add/remove/move) on the UI/message thread under a lock.
 *  - Audio thread snapshots a shared_ptr<vector<...>> via std::atomic_load.
 *  - prepareToPlay is called on the UI thread before the slot is published.
 */
class PluginChain : private juce::AudioProcessorListener
{
public:
    PluginChain();
    ~PluginChain();

    void prepare (double sampleRate, int blockSize, int numChannels);
    void releaseResources();

    /** Process in-place. Mono-broadcast input is acceptable; channel count must
     *  match `prepare`. Bypassed slots are skipped. */
    void process (juce::AudioBuffer<float>& buffer);

    // ----- Mutation (UI thread) -----
    /** Adds a prepared plugin instance to the end of the chain. Takes ownership. */
    void addPlugin (std::unique_ptr<juce::AudioPluginInstance> instance,
                    const juce::String& displayName,
                    const juce::String& identifier,
                    ns::FxCategory category = ns::FxCategory::Other);
    void removePlugin (int index);
    void moveSlot     (int from, int to);
    void setBypassed  (int index, bool b);

    /** Bypass / un-bypass every slot in the chain whose plugin matches the
     *  given category. Used by the signal-chain bar so a single click on the
     *  GATE / COMP / DRIVE / EQ / MOD / DELAY / REVERB / LIMIT / FX block
     *  flips bypass for every plugin in that category at once. */
    void setCategoryBypassed (ns::FxCategory cat, bool b);

    /** True if there is at least one slot in this category AND every slot in
     *  that category is currently bypassed. Returns false if the category is
     *  empty (so the UI doesn't paint an empty block as "bypassed"). */
    bool isCategoryBypassed (ns::FxCategory cat) const;

    /** Whole-chain bypass. When true, process() is a no-op and the host
     *  signal passes through unchanged. Exposed for MIDI-footswitch control
     *  of the entire Pre-FX or Post-FX block. */
    void setChainBypassed (bool b) noexcept { chainBypassed.store (b); }
    bool isChainBypassed() const noexcept   { return chainBypassed.load(); }

    /** Whether the audio thread has soft-bypassed this slot due to repeated
     *  CPU-budget overruns. Cleared automatically when the user toggles
     *  bypass off, or via clearAutoBypass(). */
    bool isAutoBypassed (int index) const;
    void clearAutoBypass (int index);

    /** Request a MIDI Panic to be injected at the start of the next audio block:
     *  All-Sound-Off (CC120) + All-Notes-Off (CC123) + Sustain Off (CC64=0) on
     *  every channel. RT-safe; idempotent until consumed. */
    void requestPanic() noexcept { panicPending.store (true); }

    int  getNumSlots() const;

    /** Sum of latency reported by all (non-bypassed) plugin instances, in samples.
     *  Polled on the message thread for status display; safe relative to mutations
     *  because we snapshot the live list. */
    int  getReportedLatencySamples() const;

    /** Sets a host PlayHead that will be pushed into all current and future
     *  plugin instances. Pass nullptr to clear. */
    void setHostPlayHead (juce::AudioPlayHead* ph);

    /** Snapshot list for UI inspection. Returns weak references; do not keep across mutations. */
    std::vector<PluginSlot*> getSlotsForUI() const;

    PluginSlot* getSlotForUI (int index) const;

    /** Serialize current chain (descriptions + per-plugin state) to XML. UI thread. */
    std::unique_ptr<juce::XmlElement> saveStateToXml() const;

    /** Restore chain from XML, instantiating plugins via the supplied format manager + known list.
     *  UI thread. Replaces existing chain. */
    void restoreStateFromXml (const juce::XmlElement& xml,
                              juce::AudioPluginFormatManager& formats,
                              juce::KnownPluginList& known);

    /** Pre-instantiate every unique plugin referenced in the given chain XML
     *  into an off-chain "warm pool". On subsequent restoreStateFromXml()
     *  the slow path pulls from the warm pool instead of cold-loading, so
     *  the FIRST recall of any scene is just as fast as repeat recalls.
     *
     *  Called once per chain at startup with each saved scene's chain XML
     *  (App.cpp walks all 4 scenes and warms both Pre-FX and Post-FX
     *  chains). UI thread; safe to call multiple times -- duplicate
     *  (identifier + category) entries are skipped.
     *
     *  warmPool size is soft-capped at 64 entries; oldest entries are
     *  torn down when the cap is exceeded so a pathological project with
     *  hundreds of plugins doesn't leak RAM. */
    void warmUpFromXml (const juce::XmlElement& xml,
                        juce::AudioPluginFormatManager& formats,
                        juce::KnownPluginList& known);

    void saveToFile  (const juce::File& f) const;
    bool loadFromFile (const juce::File& f,
                       juce::AudioPluginFormatManager& formats,
                       juce::KnownPluginList& known);

    /** Monotonically increasing counter bumped on every structural mutation
     *  (add / remove / move / restoreStateFromXml) AND on every hosted-
     *  plugin parameter/state change (via AudioProcessorListener). Used by
     *  PresetManager to cheaply detect whether the live chain might have
     *  diverged from a cached XML snapshot, so it can skip the expensive
     *  re-serialise on back-to-back captures when nothing has changed. */
    int  getMutationGen() const noexcept { return mutationGen.load(); }

    /** Loading-state accessors.  A "push" is an async setStateInformation
     *  call scheduled after a scene recall. While pushes are outstanding the
     *  UI should show a loading indicator so the user doesn't switch scenes
     *  before the new state has been absorbed by every plugin. */
    bool  isLoading()           const noexcept { return currentBatch && currentBatch->load() > 0; }
    int   getPushesActive()     const noexcept { return currentBatch ? std::max (0, currentBatch->load()) : 0; }
    int   getPushesScheduled()  const noexcept { return batchPushTotal.load(); }

private:
    using SlotList = std::vector<std::shared_ptr<PluginSlot>>;

    void publish (SlotList newList);
    SlotList currentListCopy() const;

    mutable std::mutex mutationMutex;
    std::shared_ptr<SlotList> liveList; // accessed via std::atomic_load/store

    // Off-chain pool of pre-instantiated, parked (autoBypassed) plugin
    // instances. restoreStateFromXml() draws from this pool before cold-
    // instantiating, and returns unmatched-but-still-loaded slots back to
    // it instead of tearing them down -- so scene switches reuse already
    // loaded plugins (NDSP Archetype, Klirton Grindstein, etc.) instead
    // of paying their multi-hundred-ms instantiation + prepareToPlay cost
    // every time the user flips to a scene that uses them again.
    // Protected by mutationMutex. Soft-capped at 64 entries.
    std::vector<std::shared_ptr<PluginSlot>> warmPool;

    double sampleRate { 48000.0 };
    int    blockSize  { 0 };
    int    numChannels { 2 };
    juce::AudioPlayHead* hostPlayHead { nullptr };

    juce::AudioBuffer<float> stereoScratch; // for plugins that want stereo
    juce::AudioBuffer<float> dryScratch;    // per-slot pre-process copy for park crossfade
    juce::MidiBuffer         midiScratch;
    std::atomic<bool>        panicPending  { false };
    std::atomic<bool>        chainBypassed { false };
    std::atomic<int>         mutationGen   { 0 };

    // Background state-push tracking for the loading UI.
    // currentBatch: per-restore-call shared counter; decremented by every push
    //   on completion regardless of bail/success. Old lambdas hold a shared_ptr
    //   to the OLD counter so they can't corrupt the current batch's count.
    // batchPushTotal: count of pushes in the most recent batch (for progress %).
    std::shared_ptr<std::atomic<int>> currentBatch;
    std::atomic<int>  batchPushTotal { 0 };

    // Per-category bypass override. Persists even when the category contains
    // zero plugins, so a click on a signal-chain block (e.g. MASTER FX)
    // always produces an immediate visual toggle and the override is
    // re-applied to any plugin later added to that category.
    static constexpr int kNumCategories = static_cast<int> (ns::FxCategory::Other) + 1;
    std::array<std::atomic<bool>, (size_t) kNumCategories> categoryBypassOverride {};

    // AudioProcessorListener -- fired by hosted plugins when their
    // parameters or general state change. We use these to invalidate the
    // PresetManager XML cache by bumping mutationGen.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override
    {
        mutationGen.fetch_add (1, std::memory_order_relaxed);
    }
    void audioProcessorChanged (juce::AudioProcessor*,
                                const ChangeDetails&) override
    {
        mutationGen.fetch_add (1, std::memory_order_relaxed);
    }
};
