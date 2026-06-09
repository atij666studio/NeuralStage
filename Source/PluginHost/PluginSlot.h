#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>
#include "PluginCategory.h"

/** A single plugin in the FX chain. */
struct PluginSlot
{
    juce::String                               identifier;   // pluginFormatName + fileOrIdentifier
    juce::String                               displayName;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::atomic<bool>                          bypassed { false };
    ns::FxCategory                             category { ns::FxCategory::Other };

    // CPU-spike auto-bypass state (consumed by PluginChain).
    // autoBypassed is set when the plugin overruns the per-block budget
    // for too many consecutive blocks; flips back to false on user un-bypass.
    std::atomic<bool>                          autoBypassed { false };
    int                                        spikeCount   { 0 };

    // Hash (juce::String::hashCode64) of the most recently applied
    // state-information base64 blob, so the scene-recall fast-path can
    // skip pushing setStateInformation when the new state is identical
    // to what's already loaded. Avoids audible glitches (silent gaps,
    // tail loss, hiss bursts) in plugins whose setStateInformation
    // re-prepares internal buffers (IR loaders, delays, reverbs).
    juce::int64                                lastAppliedStateHash { 0 };

    // Generation counter for background state-push threads. Each call to
    // scheduleBackgroundStatePush increments this; the background thread checks
    // it before calling setStateInformation and bails if a newer generation has
    // superseded it. Prevents concurrent state mutations from rapid scene switches.
    std::atomic<int>                           statePushGen { 0 };

    // Current wet gain [0..1] for the park/unpark crossfade ramp (audio thread only).
    // 1.0 = fully active, 0.0 = fully parked (bypassed). Ramped per-sample over
    // ~10 ms so scene recalls don't produce audible clicks.
    float                                      currentWetGain { 1.0f };

    // Editor window (UI thread only)
    std::unique_ptr<juce::DocumentWindow>      editorWindow;
};
