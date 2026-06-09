#include "PluginChain.h"
#include "../Utils/FileUtils.h"
#include <thread>
#include <chrono>
#include <set>

namespace
{
    // Schedule deferred teardown of a chain slot off the message thread.
    // Heavy 3rd-party plugins (NDSP Archetype, IR loaders, big convolvers)
    // can stall for hundreds of ms inside releaseResources() + destructor;
    // doing that on the message thread freezes the UI and causes the
    // visible "scene-switch lag" the user complained about. We split the
    // teardown:
    //   1. caller (already on message thread) destroys the editor window.
    //   2. background thread waits 600 ms so the audio thread can rotate
    //      past the old liveList snapshot, then calls releaseResources().
    //      This is the expensive step -- safe off-thread because the slot
    //      is no longer in any live list at this point.
    //   3. final shared_ptr drop (the instance destructor) is bounced back
    //      to the message thread, where it must run for 3rd-party plugins
    //      whose destructors touch GUI / COM apartment state.
    inline void scheduleBackgroundSlotTeardown (std::shared_ptr<PluginSlot> doomed)
    {
        if (! doomed) return;
        doomed->editorWindow.reset();
        std::thread ([doomed]() mutable
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (600));
            // releaseResources() must be called on the message thread.
            // VST3/CLAP plugins may touch COM, GUI objects, or WinRT in their
            // cleanup code — invoking this from a background thread crashes many
            // commercial plugins (NDSP Archetype, Klirton, etc.) even though the
            // slot is no longer in the live audio list at this point.
            juce::MessageManager::callAsync ([doomed]() mutable
            {
                if (doomed && doomed->instance)
                    doomed->instance->releaseResources();
                doomed.reset();
            });
        }).detach();
    }

    // Apply setStateInformation() off the message thread.
    //
    // Heavy plugins (NAM model load, IR convolver kernel rebuild, NDSP
    // Archetype, Klirton Grindstein, etc.) can spend hundreds of ms inside
    // setStateInformation -- mostly file I/O, model parsing and
    // convolution-kernel FFT setup. Doing that on the message thread is
    // the dominant cause of "scene N -> scene 1" feeling laggy even when
    // the new scene is smaller, because we still have to push fresh
    // presets into the reused NAM / IR slots.
    //
    // The slot is parked under autoBypassed BEFORE this is called, so the
    // audio thread silently skips it while the new state loads. Once the
    // state is in, autoBypassed is restored on the message thread via
    // callAsync (most plugins assume their internal listener callbacks
    // fire on the message thread, so we keep the bypass-flip there even
    // though the flag itself is atomic).
    //
    // Generation guard (statePushGen): each call increments the slot's
    // counter and captures the new value as myGen. If the user switches
    // scenes again before this thread wakes, statePushGen will have
    // advanced, and we bail without touching setStateInformation. This
    // eliminates the concurrent-mutation crash on rapid scene switching.
    inline void scheduleBackgroundStatePush (std::shared_ptr<PluginSlot> slot,
                                             juce::MemoryBlock mb,
                                             juce::int64 hash,
                                             bool restoreAutoBypassTo,
                                             std::shared_ptr<std::atomic<int>> batchRef)
    {
        // Count this push in the batch counter up front (before the early-exit
        // check) so the caller can read batchRef->load() after all calls to get
        // the exact scheduled count.  Every code path below is responsible for
        // one matching fetch_sub(1) via callAsync on the message thread.
        if (batchRef) batchRef->fetch_add (1);

        if (! slot || ! slot->instance)
        {
            if (batchRef)
                juce::MessageManager::callAsync ([batchRef] { batchRef->fetch_sub (1); });
            return;
        }

        // Claim this generation. Any previously spawned thread for this slot
        // will see a different value and bail before calling setStateInformation.
        const int myGen = slot->statePushGen.fetch_add (1) + 1;

        std::thread ([slot, mb = std::move (mb), hash, restoreAutoBypassTo, myGen, batchRef]() mutable
        {
            // 80 ms safety window:
            //   10 ms  — audio wet/dry park ramp (PluginChain::process rampStepPerSample)
            //   23 ms  — worst-case processBlock at 44.1 kHz / 1024 samples
            //   23 ms  — second block (audio thread may start a new block the
            //            moment we begin sleeping, so we need to cover it too)
            //   24 ms  — OS scheduler jitter + callback overhead margin
            std::this_thread::sleep_for (std::chrono::milliseconds (80));

            // Bail if a newer push has been scheduled for this slot.
            if (slot->statePushGen.load() != myGen)
            {
                // Decrement on the message thread (same queue where callAsync
                // consumers run) to keep all counter mutations serial.
                if (batchRef)
                    juce::MessageManager::callAsync ([batchRef] { batchRef->fetch_sub (1); });
                return;
            }

            // setStateInformation MUST run on the message thread.
            //
            // Many commercial VST3/CLAP plugins (NDSP Archetype, Klirton, IR
            // loaders) are NOT thread-safe for state loading. Their setState()
            // implementations touch COM apartments, WinRT thread-local state,
            // or internal GUI objects that are bound to the main thread.
            // Calling setStateInformation from a background thread crashes
            // these plugins immediately, even when the audio thread has already
            // stopped calling processBlock (autoBypassed=true, ramp at 0).
            //
            // We accept that heavy plugins (NAM model load 0.5-2 s, IR kernel
            // FFT setup) will briefly block the message thread during this call.
            // The warm pool's tier-1 hash match prevents state pushes on repeat
            // scene switches, so this path only fires when the state genuinely
            // differs from what the slot already has loaded.
            juce::MessageManager::callAsync (
                [slot, mb = std::move (mb), hash, restoreAutoBypassTo, myGen, batchRef]() mutable
                {
                    if (slot->statePushGen.load() != myGen)
                    {
                        if (batchRef) batchRef->fetch_sub (1);
                        return;
                    }

                    if (slot && slot->instance)
                    {
                        try
                        {
                            slot->instance->setStateInformation (mb.getData(), (int) mb.getSize());
                            slot->lastAppliedStateHash = hash;
                            // reset() after state load wakes lazy-initializing
                            // plugins (e.g. NDSP Archetype) that don't produce
                            // audio until their processing engine is re-kicked.
                            // Equivalent to what opening/closing the editor does.
                            slot->instance->reset();
                        }
                        catch (...)
                        {
                            juce::Logger::writeToLog ("PluginChain: setStateInformation threw "
                                                     "on message thread for slot: "
                                                     + slot->displayName);
                        }
                    }

                    // Second generation check: a rapid scene switch may have
                    // arrived while we were inside setStateInformation.
                    if (slot->statePushGen.load() != myGen)
                    {
                        if (batchRef) batchRef->fetch_sub (1);
                        return;
                    }

                    if (slot)
                        slot->autoBypassed.store (restoreAutoBypassTo);

                    if (batchRef) batchRef->fetch_sub (1);
                });
        }).detach();
    }
    // Mirrors PluginManager's pedal/marker file locations. Kept here so the
    // chain restore path stays self-contained and doesn't pull in
    // PluginManager (which would create a circular dependency).
    juce::File deadMansPedalFile()
    {
        return ns::FileUtils::userDataDir().getChildFile ("DeadMansPedal.txt");
    }

    juce::File pendingAddFile()
    {
        return ns::FileUtils::userDataDir().getChildFile ("PendingAdd.txt");
    }

    juce::StringArray loadBlacklist()
    {
        const auto pedal = deadMansPedalFile();
        juce::StringArray lines;
        if (pedal.existsAsFile())
        {
            lines = juce::StringArray::fromLines (pedal.loadFileAsString());
            lines.removeEmptyStrings();
        }
        return lines;
    }

    void appendBlacklist (const juce::String& fileId)
    {
        auto lines = loadBlacklist();
        if (! lines.contains (fileId))
            lines.add (fileId);
        deadMansPedalFile().replaceWithText (lines.joinIntoString ("\n") + "\n");
    }
}

PluginChain::PluginChain()
{
    auto initial = std::make_shared<SlotList>();
    std::atomic_store (&liveList, initial);
}

PluginChain::~PluginChain()
{
    // Detach listeners from every still-living instance BEFORE they (and
    // we) are destroyed -- defends against a hosted plugin firing a final
    // audioProcessorChanged into a half-destroyed PluginChain.
    if (auto list = std::atomic_load (&liveList))
        for (auto& s : *list)
            if (s && s->instance) s->instance->removeListener (this);
    for (auto& s : warmPool)
        if (s && s->instance) s->instance->removeListener (this);

    releaseResources();
}

void PluginChain::prepare (double sr, int bs, int nc)
{
    sampleRate  = sr;
    blockSize   = bs;
    numChannels = juce::jmax (1, nc);

    stereoScratch.setSize (numChannels, blockSize, false, false, true);
    dryScratch.setSize    (numChannels, blockSize, false, false, true);

    auto list = std::atomic_load (&liveList);
    if (list != nullptr)
        for (auto& s : *list)
            if (s && s->instance)
                s->instance->prepareToPlay (sampleRate, blockSize);

    // Re-prepare warm-pool entries too so they're ready to drop straight
    // into the chain on the next scene recall without an extra prepare
    // round-trip.
    for (auto& s : warmPool)
        if (s && s->instance)
            s->instance->prepareToPlay (sampleRate, blockSize);
}

void PluginChain::releaseResources()
{
    auto list = std::atomic_load (&liveList);
    if (list != nullptr)
        for (auto& s : *list)
            if (s && s->instance)
                s->instance->releaseResources();
    for (auto& s : warmPool)
        if (s && s->instance)
            s->instance->releaseResources();
}

void PluginChain::process (juce::AudioBuffer<float>& buffer)
{
    if (chainBypassed.load()) return;

    auto list = std::atomic_load (&liveList);
    if (list == nullptr || list->empty()) return;

    midiScratch.clear();

    if (panicPending.exchange (false))
    {
        // Inject MIDI panic: CC120 (All Sound Off), CC123 (All Notes Off),
        // CC64=0 (Sustain Off) on every channel, at sample 0.
        for (int ch = 1; ch <= 16; ++ch)
        {
            midiScratch.addEvent (juce::MidiMessage::controllerEvent (ch, 120, 0), 0);
            midiScratch.addEvent (juce::MidiMessage::controllerEvent (ch, 123, 0), 0);
            midiScratch.addEvent (juce::MidiMessage::controllerEvent (ch,  64, 0), 0);
        }
    }

    const int n = buffer.getNumSamples();
    const int incomingChans = buffer.getNumChannels();

    // Resize scratch if buffer grew unexpectedly (RT-safe path keeps it preallocated;
    // this is a defensive resize that should not normally trigger).
    if (stereoScratch.getNumSamples() < n || stereoScratch.getNumChannels() != numChannels)
        stereoScratch.setSize (numChannels, juce::jmax (n, blockSize), false, false, true);
    if (dryScratch.getNumSamples() < n || dryScratch.getNumChannels() != numChannels)
        dryScratch.setSize (numChannels, juce::jmax (n, blockSize), false, false, true);

    // Copy incoming -> scratch (broadcast mono if needed).
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const int srcCh = juce::jmin (ch, incomingChans - 1);
        juce::FloatVectorOperations::copy (stereoScratch.getWritePointer (ch),
                                           buffer.getReadPointer (srcCh), n);
    }

    juce::AudioBuffer<float> proc (stereoScratch.getArrayOfWritePointers(), numChannels, n);

    // Per-block budget for CPU-spike auto-bypass: this WAS an automatic
    // safety net that soft-bypassed any plugin spending >85% of one block
    // budget for 8 consecutive blocks. In practice it caused legitimate
    // heavy plugins (NDSP Archetype, IR convolvers, hi-res reverbs) to
    // get silently bypassed after a scene recall transient -- the user's
    // chain would suddenly stop producing sound with no visible cause,
    // and re-saving scenes would propagate the stuck "auto-bypassed"
    // state into more slots. The auto-bypass action is now DISABLED.
    // We still time the blocks so the diagnostics counter is available
    // for the UI to surface slow plugins, but we never silently mute
    // anything. If a plugin genuinely hangs, the OS audio glitch will
    // surface it -- which the user can act on -- instead of the host
    // silently dropping that slot.
    constexpr float kSpikeRatio  = 0.85f;
    const double blockSeconds = (sampleRate > 0.0) ? (double) n / sampleRate : 0.0;
    const double budgetSeconds = blockSeconds * (double) kSpikeRatio;

    // Wet/dry park ramp: per-sample step that moves currentWetGain from
    // its current value toward target (0 = parked / bypassed, 1 = active)
    // over ~10 ms. This replaces the previous "hard skip on autoBypassed"
    // which produced an audible click on every scene recall (the slot's
    // output jumped instantly from full to zero, then back to full when
    // the background state push finished). With the ramp the user hears
    // the dry input fade in over the wet output instead of a discontinuity.
    const float rampStepPerSample = (sampleRate > 0.0)
        ? (1.0f / (0.010f * (float) sampleRate)) : 1.0f;

    for (auto& slot : *list)
    {
        if (slot == nullptr || slot->instance == nullptr) continue;

        const bool  hardBypass = slot->bypassed.load();
        const bool  parked     = slot->autoBypassed.load();
        const float target     = (hardBypass || parked) ? 0.0f : 1.0f;
        const float curStart   = slot->currentWetGain;

        // Fast path: fully parked AND already at zero -- skip entirely
        // (no CPU, no ramp work). Matches the old hard-skip behaviour
        // once the ramp has fully settled into the parked state.
        if (target <= 0.0f && curStart <= 0.0001f)
        {
            slot->currentWetGain = 0.0f;
            continue;
        }

        // Fast path: fully active AND already at one -- no crossfade
        // needed, just process in place (the common steady-state case
        // when nothing is being recalled).
        const bool needCrossfade = ! (target >= 1.0f && curStart >= 0.9999f);

        if (needCrossfade)
        {
            // Snapshot dry input BEFORE processing so we can blend it
            // back in proportion to (1 - currentWetGain) per sample.
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::copy (dryScratch.getWritePointer (ch),
                                                   proc.getReadPointer (ch), n);
        }

        const auto t0 = juce::Time::getHighResolutionTicks();
        slot->instance->processBlock (proc, midiScratch);
        const auto t1 = juce::Time::getHighResolutionTicks();
        const double elapsed = juce::Time::highResolutionTicksToSeconds (t1 - t0);

        if (needCrossfade)
        {
            float g = curStart;
            for (int s = 0; s < n; ++s)
            {
                if      (g < target) g = juce::jmin (target, g + rampStepPerSample);
                else if (g > target) g = juce::jmax (target, g - rampStepPerSample);
                const float dryG = 1.0f - g;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* p = proc.getWritePointer (ch);
                    auto* d = dryScratch.getReadPointer (ch);
                    p[s] = p[s] * g + d[s] * dryG;
                }
            }
            slot->currentWetGain = g;
        }

        if (budgetSeconds > 0.0 && elapsed > budgetSeconds)
        {
            // Diagnostic only: count consecutive over-budget blocks. Capped
            // so it can't overflow. Auto-bypass action removed (see comment
            // above) -- previously this set slot->autoBypassed.store (true)
            // after 8 consecutive spikes, which permanently muted heavy
            // plugins after a scene-switch transient and was the dominant
            // cause of the "scenes silently lose plugins" bug.
            if (slot->spikeCount < 1000) ++slot->spikeCount;
        }
        else if (slot->spikeCount > 0)
        {
            --slot->spikeCount; // recover slowly
        }
    }

    // Copy back to host buffer.
    for (int ch = 0; ch < incomingChans; ++ch)
    {
        const int srcCh = juce::jmin (ch, numChannels - 1);
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch),
                                           proc.getReadPointer (srcCh), n);
    }
}

PluginChain::SlotList PluginChain::currentListCopy() const
{
    auto src = std::atomic_load (&liveList);
    if (src == nullptr) return {};
    return *src;
}

void PluginChain::publish (SlotList newList)
{
    auto shared = std::make_shared<SlotList> (std::move (newList));
    std::atomic_store (&liveList, shared);
    mutationGen.fetch_add (1);
}

void PluginChain::addPlugin (std::unique_ptr<juce::AudioPluginInstance> instance,
                             const juce::String& displayName,
                             const juce::String& identifier,
                             ns::FxCategory category)
{
    if (instance == nullptr) return;

    instance->setPlayConfigDetails (numChannels, numChannels, sampleRate, blockSize);
    instance->prepareToPlay (sampleRate, blockSize);
    if (hostPlayHead != nullptr) instance->setPlayHead (hostPlayHead);

    // Listen for parameter / state changes so the PresetManager XML cache
    // is invalidated whenever the user (or automation) touches a knob
    // inside a hosted plugin. Without this the cache key (mutationGen)
    // would only track structural changes and back-to-back captures of
    // different live states would return identical stale XML.
    instance->addListener (this);

    auto slot = std::make_shared<PluginSlot>();
    slot->instance    = std::move (instance);
    slot->displayName = displayName;
    slot->identifier  = identifier;
    slot->category    = category;

    // Inherit any pending category-bypass override so a plugin dropped into
    // a category the user previously toggled "bypassed" starts bypassed.
    {
        const int idx = static_cast<int> (category);
        if (idx >= 0 && idx < kNumCategories
            && categoryBypassOverride[(size_t) idx].load())
        {
            slot->bypassed.store (true);
        }
    }

    std::lock_guard<std::mutex> lock (mutationMutex);
    auto list = currentListCopy();

    // Insert in category order: walk until we find the first existing slot
    // whose category sorts AFTER the new slot's category, and insert there.
    // Within the same category, append at the end (preserve add order).
    const auto myOrd = (int) category;
    auto it = list.begin();
    while (it != list.end() && (int) (*it)->category <= myOrd)
        ++it;
    list.insert (it, std::move (slot));
    publish (std::move (list));
}

void PluginChain::removePlugin (int index)
{
    std::shared_ptr<PluginSlot> doomed;
    {
        std::lock_guard<std::mutex> lock (mutationMutex);
        auto list = currentListCopy();
        if (index < 0 || index >= (int) list.size()) return;
        doomed = list[(size_t) index];
        list.erase (list.begin() + index);
        publish (std::move (list));
    }
    // The audio thread may still hold a reference to the OLD liveList snapshot
    // (which still contains `doomed`). Calling releaseResources() now would
    // pull buffers out from under an in-flight processBlock() and SIGSEGV
    // inside the plugin (seen with A1StereoControl AU during A/B switches).
    //
    // Solution: keep `doomed` alive and defer teardown by ~600 ms -- long
    // enough for the audio thread to rotate past the old snapshot for any
    // realistic block size at any sample rate (256 samples @ 44.1 kHz ~5.8 ms;
    // 600 ms gives a 100x safety margin).
    if (doomed)
    {
        // Editor first, on UI thread (we're on UI thread here) -- safe because
        // it doesn't touch audio-thread-visible state. The heavy releaseResources
        // call + 600 ms audio-thread rotation wait run on a background thread
        // so they don't stall the UI on scene switches with NDSP / convolver
        // plugins (see scheduleBackgroundSlotTeardown at top of file).
        scheduleBackgroundSlotTeardown (doomed);
    }
}

void PluginChain::moveSlot (int from, int to)
{
    std::lock_guard<std::mutex> lock (mutationMutex);
    auto list = currentListCopy();
    if (from < 0 || from >= (int) list.size()) return;
    to = juce::jlimit (0, (int) list.size() - 1, to);
    auto s = list[(size_t) from];
    list.erase (list.begin() + from);
    list.insert (list.begin() + to, s);
    publish (std::move (list));
}

void PluginChain::setBypassed (int index, bool b)
{
    auto list = std::atomic_load (&liveList);
    if (list == nullptr || index < 0 || index >= (int) list->size()) return;
    (*list)[(size_t) index]->bypassed.store (b);
    // User-driven bypass clears any CPU-spike auto-bypass latch as well.
    (*list)[(size_t) index]->autoBypassed.store (false);
    (*list)[(size_t) index]->spikeCount = 0;
}

bool PluginChain::isAutoBypassed (int index) const
{
    auto list = std::atomic_load (&liveList);
    if (list == nullptr || index < 0 || index >= (int) list->size()) return false;
    return (*list)[(size_t) index]->autoBypassed.load();
}

void PluginChain::clearAutoBypass (int index)
{
    auto list = std::atomic_load (&liveList);
    if (list == nullptr || index < 0 || index >= (int) list->size()) return;
    (*list)[(size_t) index]->autoBypassed.store (false);
    (*list)[(size_t) index]->spikeCount = 0;
}

void PluginChain::setCategoryBypassed (ns::FxCategory cat, bool b)
{
    // Always remember the user's intent -- even when the category currently
    // has zero plugins -- so the UI badge toggles immediately on click and
    // any plugin later added to this category inherits the bypass state.
    const int idx = static_cast<int> (cat);
    if (idx >= 0 && idx < kNumCategories)
        categoryBypassOverride[(size_t) idx].store (b);

    auto list = std::atomic_load (&liveList);
    if (list == nullptr) return;
    for (auto& s : *list)
    {
        if (s && s->category == cat)
        {
            s->bypassed.store (b);
            s->autoBypassed.store (false);
            s->spikeCount = 0;
        }
    }
}

bool PluginChain::isCategoryBypassed (ns::FxCategory cat) const
{
    const int idx = static_cast<int> (cat);
    const bool override_ = (idx >= 0 && idx < kNumCategories)
                              ? categoryBypassOverride[(size_t) idx].load()
                              : false;

    auto list = std::atomic_load (&liveList);
    if (list == nullptr) return override_;
    bool found = false;
    for (auto& s : *list)
    {
        if (s && s->category == cat)
        {
            found = true;
            if (! s->bypassed.load()) return false;
        }
    }
    // No plugins yet in this category -- fall back to the user's stored
    // intent so a MASTER FX click toggles strikethrough even on an empty slot.
    return found ? true : override_;
}

int PluginChain::getReportedLatencySamples() const
{
    auto list = std::atomic_load (&liveList);
    if (list == nullptr) return 0;
    int total = 0;
    for (auto& s : *list)
        if (s && s->instance && ! s->bypassed.load() && ! s->autoBypassed.load())
            total += juce::jmax (0, s->instance->getLatencySamples());
    return total;
}

int PluginChain::getNumSlots() const
{
    auto list = std::atomic_load (&liveList);
    return list ? (int) list->size() : 0;
}

void PluginChain::setHostPlayHead (juce::AudioPlayHead* ph)
{
    hostPlayHead = ph;
    auto list = std::atomic_load (&liveList);
    if (list == nullptr) return;
    for (auto& s : *list)
        if (s && s->instance)
            s->instance->setPlayHead (ph);
}

std::vector<PluginSlot*> PluginChain::getSlotsForUI() const
{
    auto list = std::atomic_load (&liveList);
    std::vector<PluginSlot*> out;
    if (list == nullptr) return out;
    out.reserve (list->size());
    for (auto& s : *list) out.push_back (s.get());
    return out;
}

PluginSlot* PluginChain::getSlotForUI (int index) const
{
    auto list = std::atomic_load (&liveList);
    if (list == nullptr || index < 0 || index >= (int) list->size()) return nullptr;
    return (*list)[(size_t) index].get();
}

std::unique_ptr<juce::XmlElement> PluginChain::saveStateToXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("PluginChain");
    auto list = std::atomic_load (&liveList);
    if (list == nullptr) return root;

    for (auto& slot : *list)
    {
        if (slot == nullptr || slot->instance == nullptr) continue;

        juce::PluginDescription desc;
        slot->instance->fillInPluginDescription (desc);

        auto* slotXml = root->createNewChildElement ("Slot");
        slotXml->setAttribute ("bypassed",     slot->bypassed.load());
        slotXml->setAttribute ("displayName",  slot->displayName);
        slotXml->setAttribute ("category",     (int) slot->category);

        if (auto descXml = desc.createXml())
            slotXml->addChildElement (descXml.release());

        // Suspend the plugin's processing for the duration of the state
        // serialization. Many plugins (especially older / 3rd-party amp-sims
        // and EQs) mutate internal state inside processBlock and are NOT
        // safe to call getStateInformation on concurrently -- that race was
        // observed to crash the host when capturing scenes while audio was
        // running. JUCE's suspendProcessing() asks the plugin to stop and
        // waits for any in-flight processBlock to drain, then we restore.
        juce::MemoryBlock state;
        {
            const bool wasSuspended = slot->instance->isSuspended();
            if (! wasSuspended) slot->instance->suspendProcessing (true);
            slot->instance->getStateInformation (state);
            if (! wasSuspended) slot->instance->suspendProcessing (false);
        }
        if (state.getSize() > 0)
        {
            auto* stateXml = slotXml->createNewChildElement ("State");
            stateXml->addTextElement (state.toBase64Encoding());
        }
    }
    return root;
}

void PluginChain::restoreStateFromXml (const juce::XmlElement& xml,
                                       juce::AudioPluginFormatManager& formats,
                                       juce::KnownPluginList& known)
{
    if (! xml.hasTagName ("PluginChain")) return;

    // Fresh batch counter for this restore call.  Shared with every
    // scheduleBackgroundStatePush lambda so old lambdas from a prior
    // scene switch decrement their own (now-orphaned) counter, never
    // corrupting this batch's count.
    auto batchRef = std::make_shared<std::atomic<int>> (0);
    currentBatch = batchRef;

    // -------- Fast path: in-place state restore --------
    // If the new chain has the same set of plugins in the same order
    // (matched by createIdentifierString + category + display name + bypass),
    // we can just push setStateInformation into the existing instances
    // instead of tearing down and rebuilding the whole chain. This dodges:
    //   * audible scene-switch silence gap
    //   * 600 ms-per-slot deferred teardown pileup on rapid switching
    //   * crashes from buggy 3rd-party plugin destructors / constructors
    //     fired in quick succession during back-to-back scene recalls
    {
        struct IncomingSlot
        {
            juce::PluginDescription desc;
            juce::String            displayName;
            ns::FxCategory          category;
            bool                    bypassed;
            juce::String            stateB64;
        };
        std::vector<IncomingSlot> incoming;
        for (auto* slotXml : xml.getChildWithTagNameIterator ("Slot"))
        {
            auto* descXml = slotXml->getChildByName ("PLUGIN");
            if (descXml == nullptr) continue;
            IncomingSlot s;
            if (! s.desc.loadFromXml (*descXml)) continue;
            s.displayName = slotXml->getStringAttribute ("displayName",
                              s.desc.name + " (" + s.desc.pluginFormatName + ")");
            s.bypassed = slotXml->getBoolAttribute ("bypassed", false);
            s.category = (ns::FxCategory) slotXml->getIntAttribute ("category",
                              (int) ns::FxCategory::Other);
            // .trim() must match the trimming done in warmUpFromXml so that
            // the state-hash compare in findDonor's tier-1 (warm-pool) match
            // actually hits -- otherwise every first scene-recall falls back
            // to a background state push and feels laggy.
            if (auto* st = slotXml->getChildByName ("State"))
                s.stateB64 = st->getAllSubText().trim();
            incoming.push_back (std::move (s));
        }

        auto live = std::atomic_load (&liveList);
        const bool sameComposition = (live != nullptr)
                                  && (live->size() == incoming.size());
        bool allMatch = sameComposition;
        if (allMatch)
        {
            for (size_t k = 0; k < incoming.size(); ++k)
            {
                auto& cur = (*live)[k];
                if (cur == nullptr || cur->instance == nullptr) { allMatch = false; break; }
                juce::PluginDescription curDesc;
                cur->instance->fillInPluginDescription (curDesc);
                if (curDesc.createIdentifierString() != incoming[k].desc.createIdentifierString()
                    || cur->category != incoming[k].category)
                { allMatch = false; break; }
            }
        }
        if (allMatch)
        {
            for (size_t k = 0; k < incoming.size(); ++k)
            {
                auto& cur = (*live)[k];
                cur->displayName = incoming[k].displayName;
                cur->bypassed.store (incoming[k].bypassed);

                if (incoming[k].stateB64.isEmpty())
                {
                    // Same composition, no captured state: still scrub any
                    // stale "loading in progress" auto-bypass flag so the
                    // slot is audible. Pre-fix builds could leave it stuck
                    // true via the now-removed CPU-spike auto-bypass.
                    cur->autoBypassed.store (false);
                    cur->spikeCount = 0;
                    continue;
                }

                // Skip the state push entirely if the incoming state
                // is byte-identical to what we last pushed into this
                // plugin. Without this, every scene recall calls
                // setStateInformation on EVERY plugin, even those
                // whose state hasn't changed -- and many plugins
                // (IR loaders, delays, reverbs, some EQs) re-prepare
                // internal buffers or briefly mute on a state load,
                // producing the silent gap + hiss tail the user hears.
                const auto incomingHash = incoming[k].stateB64.hashCode64();
                if (incomingHash == cur->lastAppliedStateHash)
                {
                    // Same state already loaded: also scrub any stale
                    // auto-bypass from a prior aborted load or pre-fix
                    // CPU-spike trip so the slot is audible.
                    cur->autoBypassed.store (false);
                    cur->spikeCount = 0;
                    continue;
                }

                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (incoming[k].stateB64))
                {
                    // Park the slot, then push the state on a background
                    // thread so the heavy work (NAM model load, IR kernel
                    // FFT rebuild, etc.) doesn't freeze the message thread
                    // -- this was the dominant cause of "switching to
                    // scene 1 still lags" even for tiny target chains.
                    // Always restore to FALSE: autoBypassed is strictly a
                    // "loading in progress" gate, not a permanent flag.
                    cur->statePushGen.fetch_add (1); // cancel any in-flight push for this slot
                    cur->autoBypassed.store (true);
                    cur->spikeCount = 0;
                    scheduleBackgroundStatePush (cur, std::move (mb),
                                                 incomingHash, false, batchRef);
                }
            }
            batchPushTotal.store (batchRef->load()); // snapshot count before any async decrement
            return; // done -- in-place restore, no teardown / rebuild
        }
    }

    // -------- Slow path: composition changed --------
    // Previously this tore the whole chain down and re-instantiated every
    // plugin from XML. That meant any scene switch that ADDED or REMOVED
    // even a single plugin (e.g. clean scene 1 -> crunch scene 2 with an
    // extra Archetype) paid full cold-load cost on EVERY plugin in the new
    // chain, even ones already loaded -- and heavy NDSP / IR-loader plugins
    // can take hundreds of ms each to prepareToPlay().
    //
    // New approach: greedy additive diff.
    //   1. Collect the live chain into a "donor pool" (one entry per slot).
    //   2. Walk incoming slots in order. For each one, try to claim a donor
    //      with matching createIdentifierString + category. Matched donors
    //      reuse their instance via setStateInformation (with hash-skip).
    //      Unmatched incoming slots cold-instantiate.
    //   3. Build the new ordered list from claimed/new slots.
    //   4. Unclaimed donors are scheduled for deferred teardown (same
    //      600 ms safety used by removePlugin()).
    //   5. ONE publish() at the end, then bypass / state pushes are applied
    //      in place on the new list.
    //
    // Net effect on a clean->crunch switch where 2 of 3 plugins already
    // exist: we cold-load only the 1 new plugin instead of all 3.
    struct IncomingSlot2
    {
        juce::PluginDescription desc;
        juce::String            displayName;
        ns::FxCategory          category;
        bool                    bypassed;
        juce::String            stateB64;
    };
    std::vector<IncomingSlot2> incoming;
    for (auto* slotXml : xml.getChildWithTagNameIterator ("Slot"))
    {
        auto* descXml = slotXml->getChildByName ("PLUGIN");
        if (descXml == nullptr)
        {
            descXml = slotXml->getFirstChildElement();
            while (descXml != nullptr && descXml->getTagName() == "State")
                descXml = descXml->getNextElement();
        }
        if (descXml == nullptr) continue;
        IncomingSlot2 s;
        if (! s.desc.loadFromXml (*descXml)) continue;
        s.displayName = slotXml->getStringAttribute ("displayName",
                          s.desc.name + " (" + s.desc.pluginFormatName + ")");
        s.bypassed = slotXml->getBoolAttribute ("bypassed", false);
        s.category = (ns::FxCategory) slotXml->getIntAttribute ("category",
                          (int) ns::FxCategory::Other);
        // .trim() must match warmUpFromXml so the warm-pool hash key compare
        // hits on first recall (otherwise tier-1 always misses).
        if (auto* st = slotXml->getChildByName ("State"))
            s.stateB64 = st->getAllSubText().trim();
        incoming.push_back (std::move (s));
    }

    // Donor pool: live-chain slots PLUS the warm pool of pre-instantiated
    // off-chain slots. Reusing from either side dodges cold-instantiation
    // cost (which for heavy plugins -- NDSP Archetype, Klirton Grindstein,
    // big IR convolvers -- is the dominant scene-switch latency).
    // claimed[k] is true once a donor has been reused by an incoming slot.
    std::vector<std::shared_ptr<PluginSlot>> donors;
    if (auto live = std::atomic_load (&liveList))
        donors.insert (donors.end(), live->begin(), live->end());
    donors.insert (donors.end(), warmPool.begin(), warmPool.end());
    std::vector<bool> claimed (donors.size(), false);

    // Two-tier match: first try to find a donor whose lastAppliedStateHash
    // ALREADY equals the incoming stateB64 hash -- that means the warm-pool
    // slot was preloaded with this exact state at boot, so Pass 2 will skip
    // the state push entirely and the recall is truly instant. Fall back to
    // any same-type donor (will incur a background state push).
    auto findDonor = [&] (const IncomingSlot2& want) -> int
    {
        const auto wantId   = want.desc.createIdentifierString();
        const auto wantHash = want.stateB64.isNotEmpty()
                                ? want.stateB64.hashCode64() : (juce::int64) 0;

        int typeOnlyMatch = -1;
        for (size_t k = 0; k < donors.size(); ++k)
        {
            if (claimed[k] || donors[k] == nullptr || donors[k]->instance == nullptr)
                continue;
            if (donors[k]->category != want.category)
                continue;
            juce::PluginDescription d;
            donors[k]->instance->fillInPluginDescription (d);
            if (d.createIdentifierString() != wantId)
                continue;
            // Best case: same state already loaded.
            if (wantHash != 0 && donors[k]->lastAppliedStateHash == wantHash)
                return (int) k;
            if (typeOnlyMatch < 0)
                typeOnlyMatch = (int) k;
        }
        return typeOnlyMatch;
    };

    const auto blacklist = loadBlacklist();
    const auto pendingMarker = pendingAddFile();

    SlotList newList;
    newList.reserve (incoming.size());

    // Tracks donor slots reused from the warm pool or live chain.
    // After publish we call reset() on all of them via callAsync so lazy-
    // loading plugins (NDSP Archetype) wake their audio engine without
    // requiring the user to open and close the plugin editor window.
    std::vector<std::shared_ptr<PluginSlot>> reuseSlots;

    // Pass 1: build the new list, reusing donors where possible.
    for (const auto& want : incoming)
    {
        const int donorIdx = findDonor (want);
        if (donorIdx >= 0)
        {
            claimed[(size_t) donorIdx] = true;
            auto slot = donors[(size_t) donorIdx];
            slot->displayName = want.displayName;
            slot->bypassed.store (want.bypassed);
            // Warm-pool donors arrive parked (autoBypassed=true). Clear it
            // unconditionally here so the slot is audible once published.
            // If Pass 2 below decides this slot needs a state push, it will
            // re-park it just for the duration of the background push and
            // the bg thread's callAsync will clear it back to false again.
            // Without this, donors whose stateB64 matches their current
            // state (Pass 2 skip) would stay silent until the user toggled
            // bypass to clear autoBypassed as a side-effect.
            slot->autoBypassed.store (false);
            newList.push_back (slot);
            reuseSlots.push_back (slot); // remember for post-publish reset()
            continue;
        }

        // Need to cold-instantiate this one.
        if (blacklist.contains (want.desc.fileOrIdentifier))
            continue;

        pendingMarker.getParentDirectory().createDirectory();
        pendingMarker.replaceWithText (want.desc.fileOrIdentifier);

        juce::String err;
        auto inst = formats.createPluginInstance (want.desc, sampleRate, blockSize, err);
        if (inst == nullptr)
        {
            if (auto found = known.getTypeForIdentifierString (want.desc.createIdentifierString()))
                inst = formats.createPluginInstance (*found, sampleRate, blockSize, err);
        }
        if (inst == nullptr)
        {
            appendBlacklist (want.desc.fileOrIdentifier);
            pendingMarker.deleteFile();
            continue;
        }

        inst->setPlayConfigDetails (numChannels, numChannels, sampleRate, blockSize);
        inst->prepareToPlay (sampleRate, blockSize);
        if (hostPlayHead != nullptr) inst->setPlayHead (hostPlayHead);

        auto slot = std::make_shared<PluginSlot>();
        slot->instance    = std::move (inst);
        slot->displayName = want.displayName;
        slot->identifier  = want.desc.createIdentifierString();
        slot->category    = want.category;
        slot->bypassed.store (want.bypassed);
        newList.push_back (slot);

        pendingMarker.deleteFile();
    }

    // Pass 2: figure out which slots need a state push, park each one
    // under autoBypassed BEFORE publish, then publish, then push state
    // on a background thread. The audio thread sees the new chain
    // immediately but silently skips any slot whose state is still
    // loading -- this is what makes "scene N -> scene 1" feel instant
    // even when NAM has to load a different model file.
    auto isReusedDonor = [&] (const std::shared_ptr<PluginSlot>& s)
    {
        for (size_t k = 0; k < donors.size(); ++k)
            if (claimed[k] && donors[k] == s) return true;
        return false;
    };

    struct StatePush
    {
        std::shared_ptr<PluginSlot> slot;
        juce::MemoryBlock           mb;
        juce::int64                 hash;
        bool                        savedAutoBypass;
    };
    std::vector<StatePush> pushes;
    pushes.reserve (newList.size());
    for (size_t k = 0; k < newList.size(); ++k)
    {
        auto& slot = newList[k];
        const auto& want = incoming[k];
        if (slot == nullptr || slot->instance == nullptr || want.stateB64.isEmpty())
            continue;
        const auto h = want.stateB64.hashCode64();
        if (h == slot->lastAppliedStateHash)
            continue;
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (want.stateB64))
            continue;

        // Park the slot (autoBypassed = true) so processBlock silently
        // skips it while the background thread is applying new state.
        // Always restore to FALSE when done -- "autoBypassed" is strictly
        // a "loading in progress" gate, NOT a permanent bypass. Warm-pool
        // donors arrive with autoBypassed=true (parked off-chain); if we
        // naively captured that value they would stay silent forever after
        // being promoted into the live chain.
        // Increment statePushGen first so any background thread left over
        // from a prior scene recall on this donor slot sees a stale gen
        // and bails before touching setStateInformation.
        slot->statePushGen.fetch_add (1);
        slot->autoBypassed.store (true);
        pushes.push_back ({ slot, std::move (mb), h, false });
        (void) isReusedDonor; // suppress unused if all are cold-new
    }

    // Publish the new list (single atomic swap).
    {
        std::lock_guard<std::mutex> lock (mutationMutex);
        publish (std::move (newList));
    }

    // Post-publish: call reset() on all reused (warm-pool / live-chain)
    // donors via the message thread. This wakes NDSP Archetype and similar
    // lazy-loading plugins that don't start producing audio after a scene
    // switch unless their processing engine is explicitly re-initialized.
    // (The same effect is triggered by opening and closing the editor, but
    // we want it to happen automatically without user interaction.)
    // Slots that need a state push (Pass 2) will also receive reset() inside
    // scheduleBackgroundStatePush after setStateInformation completes.
    if (! reuseSlots.empty())
        juce::MessageManager::callAsync ([rs = std::move (reuseSlots)]() mutable
        {
            for (auto& s : rs)
                if (s && s->instance && ! s->autoBypassed.load())
                    s->instance->reset();
        });

    // Now spawn the background state pushes -- the audio thread is
    // already running the new chain, but with parked slots silenced
    // until their state finishes loading.
    for (auto& p : pushes)
        scheduleBackgroundStatePush (p.slot, std::move (p.mb),
                                     p.hash, p.savedAutoBypass, batchRef);
    batchPushTotal.store (batchRef->load()); // snapshot count before any async decrement

    // Rebuild the warm pool with every unclaimed donor (both old live-
    // chain residents and previously warmed entries). Keeping them parked
    // off-chain means the next scene switch that needs them again skips
    // cold-instantiation entirely.
    //
    // Soft-cap at 64 entries: anything beyond that gets torn down on the
    // background thread so RAM usage stays bounded on pathological
    // projects.
    constexpr size_t kWarmPoolCap = 64;
    std::vector<std::shared_ptr<PluginSlot>> newWarm;
    std::vector<std::shared_ptr<PluginSlot>> evicted;
    newWarm.reserve (donors.size());
    for (size_t k = 0; k < donors.size(); ++k)
    {
        if (claimed[k] || donors[k] == nullptr) continue;
        // Cancel any pending callAsync from a prior scheduleBackgroundStatePush
        // for this donor. Without this, the old callAsync would fire on the
        // message thread after this scene switch completes and call
        // setStateInformation on a warm-pool or evicted slot, potentially
        // with wrong state and clearing autoBypassed (un-parking the slot).
        donors[k]->statePushGen.fetch_add (1);
        if (newWarm.size() < kWarmPoolCap)
        {
            // Park the recycled slot: clear user-bypass intent (we don't
            // want a stale "bypassed" flag tagging it next time) and mark
            // autoBypassed so if it somehow makes it back on chain
            // mid-edit it's silent until intentionally repurposed.
            donors[k]->autoBypassed.store (true);
            newWarm.push_back (donors[k]);
        }
        else
        {
            evicted.push_back (donors[k]);
        }
    }
    warmPool = std::move (newWarm);

    for (auto& doomed : evicted)
        scheduleBackgroundSlotTeardown (doomed);
}

void PluginChain::warmUpFromXml (const juce::XmlElement& xml,
                                 juce::AudioPluginFormatManager& formats,
                                 juce::KnownPluginList& known)
{
    // Warm-pool entries are keyed by (identifier + category + stateHash),
    // so the SAME plugin used across multiple scenes with DIFFERENT presets
    // gets one warmed instance per unique state. This lets the slow-path
    // findDonor tier-1 match (lastAppliedStateHash == wantHash) hit on the
    // very first recall of every scene -- no setStateInformation, no
    // background push, no audible gap.
    auto keyFor = [] (const juce::String& id, ns::FxCategory cat, juce::int64 hash)
    {
        return id + "|" + juce::String ((int) cat) + "|" + juce::String (hash);
    };

    std::set<juce::String> covered;
    auto recordCovered = [&] (const std::shared_ptr<PluginSlot>& s)
    {
        if (s == nullptr || s->instance == nullptr) return;
        juce::PluginDescription d;
        s->instance->fillInPluginDescription (d);
        covered.insert (keyFor (d.createIdentifierString(), s->category, s->lastAppliedStateHash));
    };
    if (auto live = std::atomic_load (&liveList))
        for (auto& s : *live) recordCovered (s);
    for (auto& s : warmPool) recordCovered (s);

    const auto blacklist = loadBlacklist();

    for (auto* slotXml : xml.getChildWithTagNameIterator ("Slot"))
    {
        auto* descXml = slotXml->getChildByName ("PLUGIN");
        if (descXml == nullptr)
        {
            descXml = slotXml->getFirstChildElement();
            while (descXml != nullptr && descXml->getTagName() == "State")
                descXml = descXml->getNextElement();
        }
        if (descXml == nullptr) continue;

        juce::PluginDescription desc;
        if (! desc.loadFromXml (*descXml)) continue;
        const auto cat = (ns::FxCategory) slotXml->getIntAttribute ("category",
                              (int) ns::FxCategory::Other);

        // Pull the captured state once so we can dedupe by hash and apply
        // it synchronously below.
        juce::String stateB64;
        if (auto* st = slotXml->getChildByName ("State"))
            stateB64 = st->getAllSubText().trim();
        const auto stateHash = stateB64.isNotEmpty() ? stateB64.hashCode64() : (juce::int64) 0;

        const auto key = keyFor (desc.createIdentifierString(), cat, stateHash);
        if (covered.count (key)) continue;
        if (blacklist.contains (desc.fileOrIdentifier)) continue;

        juce::String err;
        auto inst = formats.createPluginInstance (desc, sampleRate, blockSize, err);
        if (inst == nullptr)
            if (auto found = known.getTypeForIdentifierString (desc.createIdentifierString()))
                inst = formats.createPluginInstance (*found, sampleRate, blockSize, err);
        if (inst == nullptr)
        {
            appendBlacklist (desc.fileOrIdentifier);
            continue;
        }

        inst->setPlayConfigDetails (numChannels, numChannels, sampleRate, blockSize);
        inst->prepareToPlay (sampleRate, blockSize);
        if (hostPlayHead != nullptr) inst->setPlayHead (hostPlayHead);

        // Synchronously apply the captured state during warm-up so the
        // warmed slot is BIT-EXACTLY ready for the first scene recall.
        // We're on the message thread at boot, audio is muted, so doing
        // this synchronously is safe and avoids the cold-state push that
        // caused the "slight lag on first recall" the user reported.
        juce::int64 appliedHash = 0;
        if (stateB64.isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (stateB64))
            {
                inst->reset();
                inst->setStateInformation (mb.getData(), (int) mb.getSize());
                inst->reset();
                appliedHash = stateHash;
            }
        }

        auto slot = std::make_shared<PluginSlot>();
        slot->instance    = std::move (inst);
        slot->displayName = slotXml->getStringAttribute ("displayName",
                              desc.name + " (" + desc.pluginFormatName + ")");
        slot->identifier  = desc.createIdentifierString();
        slot->category    = cat;
        slot->bypassed.store (false);
        slot->autoBypassed.store (true); // parked until claimed by a recall
        slot->lastAppliedStateHash = appliedHash;
        warmPool.push_back (std::move (slot));
        covered.insert (key);
    }
}

void PluginChain::saveToFile (const juce::File& f) const
{
    if (auto xml = saveStateToXml())
    {
        f.getParentDirectory().createDirectory();
        xml->writeTo (f, {});
    }
}

bool PluginChain::loadFromFile (const juce::File& f,
                                juce::AudioPluginFormatManager& formats,
                                juce::KnownPluginList& known)
{
    if (! f.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    restoreStateFromXml (*xml, formats, known);
    return true;
}
