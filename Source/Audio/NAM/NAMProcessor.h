#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <atomic>
#include <array>
#include "NAMModel.h"

namespace nam { class DSP; }

/** Runs up to 4 NAM models in parallel and blends them.
 *  Signal: pre-gain → Σ (model_i(in) * weight_i) → post-gain.
 *  Mono in / mono-broadcast out. NAM_SAMPLE compiled as float.
 */
class NAMProcessor
{
public:
    static constexpr int kNumSlots = 4;

    NAMProcessor();
    ~NAMProcessor();

    void prepare (double sampleRate, int blockSize);
    void process (juce::AudioBuffer<float>& buffer);

    // Per-slot loading (off audio thread).
    bool loadSlot   (int slot, const juce::File& file, juce::String& errorOut);
    void clearSlot  (int slot);
    bool hasSlot    (int slot) const noexcept;
    juce::String getSlotName (int slot) const;

    /** Disk file currently loaded into the slot, or an empty File if the slot
     *  is empty. Used by the autosave/restore code so a relaunch can re-load
     *  whatever .nam files the user had last time. */
    juce::File   getSlotFile (int slot) const
    {
        if (! juce::isPositiveAndBelow (slot, kNumSlots)) return {};
        return slotMeta[(size_t) slot].filePath;
    }

    /** Training sample rate stored in the model file, or 0 if the slot is empty
     *  / metadata wasn't available. Used by the UI to warn when a model trained
     *  at e.g. 48 kHz is being run inside a 96 kHz session (which shifts the
     *  model's frequency response and aliasing behaviour). */
    double getSlotSampleRate (int slot) const noexcept;

    /** True if any loaded slot's training SR differs from the current session
     *  SR by more than 1 Hz. Cheap — safe to poll from a UI timer. */
    bool hasAnySampleRateMismatch() const noexcept;

    /** Latency contributed by the oversampler when active.
     *  Returns 0 when oversampling is off or not applicable.
     *  Safe to call from the audio thread. */
    int getLatencySamples() const noexcept;

    /** User-selectable oversampling quality for the NAM inference.
     *  - Auto   : 2× at SR ≤ 88.1 kHz, off at higher SR (default).
     *  - Off    : No oversampling. Saves CPU, slight aliasing risk.
     *  - x2     : 2× always (even at 96/192 kHz sessions).
     *  - x4     : 4× always — highest quality, ~2× more CPU than x2. */
    enum class OsMode { Auto = 0, Off = 1, x2 = 2, x4 = 3 };

    void   setOversamplingMode (OsMode m) noexcept { userOsMode.store ((int) m); }
    OsMode getOversamplingMode() const noexcept    { return (OsMode) userOsMode.load(); }

    // Back-compat: slot 0.
    bool loadModelFromFile (const juce::File& file, juce::String& errorOut) { return loadSlot (0, file, errorOut); }
    void clearModel() { for (int i = 0; i < kNumSlots; ++i) clearSlot (i); }
    bool hasModel() const noexcept { for (int i = 0; i < kNumSlots; ++i) if (hasSlot (i)) return true; return false; }

    // Weights in [0..1]. Not auto-normalized — caller decides (raw mix).
    void  setSlotWeight (int slot, float w) noexcept;
    float getSlotWeight (int slot) const noexcept;

    // SlimmableModel size [0..1] (1 = full quality, 0 = minimum CPU).
    void  setSlimSize (int slot, float value) noexcept;
    float getSlimSize (int slot) const noexcept;
    bool  isSlotSlimmable (int slot) const noexcept;

    // Per-slot bypass (mutes this slot's contribution without unloading the model).
    void  setSlotBypassed (int slot, bool b) noexcept;
    bool  isSlotBypassed (int slot) const noexcept;

    // Per-slot input trim in dB, clamped to [-24, +24].
    void  setSlotInputTrimDb (int slot, float db) noexcept;
    float getSlotInputTrimDb (int slot) const noexcept;

    // Convenience: 4-source blend driven by XY in [0..1]^2 with the four
    // NAM slots anchored at the EDGE MIDPOINTS of the pad rather than the
    // corners. Mapping (cursor moves to):
    //   A = top    (0.5, 0.0)
    //   B = right  (1.0, 0.5)
    //   C = bottom (0.5, 1.0)
    //   D = left   (0.0, 0.5)
    // Centre = equal 25/25/25/25 blend of all four loaded amps.
    void setXYBlend (float x, float y) noexcept;
    float getXYBlendX() const noexcept { return xyBlendX.load(); }
    float getXYBlendY() const noexcept { return xyBlendY.load(); }

    /** Called from the message thread after loadSlot(); causes process() to snap
     *  all smoothed weights to their current targets on the next audio block,
     *  preventing the ~20 ms ramp-from-zero startup silence on A/B/C. */
    void triggerSmoothedWeightsReinit() noexcept
    {
        reinitSmoothedWeightsPending.store (true, std::memory_order_release);
    }

    void setPreGain  (float db) noexcept { preGainDb.store (db); }
    void setPostGain (float db) noexcept { postGainDb.store (db); }
    float getPreGain () const noexcept   { return preGainDb.load(); }
    float getPostGain() const noexcept   { return postGainDb.load(); }

    /** Whole-amp bypass: when true, process() leaves the input untouched
     *  (apart from re-broadcasting mono to multi-channel where needed).
     *  Cheap atomic toggle — safe to drive from MIDI footswitch. */
    void setBypassed (bool b) noexcept { bypassed.store (b); }
    bool isBypassed() const noexcept   { return bypassed.load(); }

    /** Output-loudness normalization. When enabled (default), each loaded
     *  NAM model is scaled so its measured loudness lines up at a fixed
     *  reference (-18 dBu, matching Steve Atkinson's reference plugin).
     *  This prevents the "slightly distorted / overdriven" feel on
     *  nominally clean captures, and keeps perceived level constant when
     *  swapping models. Models that don't carry loudness metadata pass
     *  through at unity. */
    void setNormalized (bool b) noexcept { normalizeEnabled.store (b); }
    bool isNormalized() const noexcept   { return normalizeEnabled.load(); }

    /** Makeup gain (dB) applied to the dry signal when no NAM model is
     *  loaded. Lets the user monitor a comfortable level instead of the
     *  raw DI when comparing patches or auditioning input. Default +12 dB
     *  roughly matches the perceived loudness of a typical loaded amp
     *  patch. Set to 0 dB for bit-exact passthrough. */
    void  setDryMakeupDb (float db) noexcept { dryMakeupDb.store (db); }
    float getDryMakeupDb() const noexcept    { return dryMakeupDb.load(); }

    /** Output-level mode — mirrors Steve Atkinson's reference Neural Amp
     *  Modeler plugin so users see familiar terminology.
     *  • Raw        — normalization OFF; you hear the capture exactly as-is
     *                  (loud captures may be "hot", quiet captures "weak").
     *  • Normalized — loudness normalization ON; every model is gain-matched
     *                  to the internal reference. Default.
     *  • Calibrated — normalization ON, intended for use with calibrated DI
     *                  input levels (matches the model's training reference).
     *                  Currently behaves identically to Normalized; the mode
     *                  is exposed so presets can record the user's intent and
     *                  the UI can show the same 3-state radio as NAM-Modeler. */
    enum class OutputMode { Raw = 0, Normalized = 1, Calibrated = 2 };
    void       setOutputMode (OutputMode m) noexcept
    {
        outputMode.store ((int) m);
        normalizeEnabled.store (m != OutputMode::Raw);
    }
    OutputMode getOutputMode() const noexcept
    {
        return (OutputMode) outputMode.load();
    }

    /** Reference output level used by the normalizer (dBu). */
    static constexpr float kReferenceDbu = -18.0f;

    //==============================================================================
    // Hosted amp-sim plugin (e.g. Neural DSP Archetype, ML Sound Lab, Bias Amp).
    // Mutually exclusive with the 4 NAM model slots from a routing standpoint:
    // when a hosted plugin is present, NAM model audio is muted and the input
    // signal is passed through the plugin instead. The NAM slots are NOT
    // erased -- clearing the hosted plugin re-enables the NAM model routing
    // immediately. The hosted plugin is configured 2-in / 2-out internally;
    // the mono input is duplicated to L+R and the plugin output is summed back
    // to mono so the rest of the post-NAM mono pipeline (autoLevel / EQ / etc.)
    // continues to work unchanged.

    /** Atomically installs `plugin` as the hosted amp-sim. Takes ownership.
     *  `plugin` must already have its bus layout configured for 2-in / 2-out;
     *  this method calls prepareToPlay(sampleRate, maxBlock) on it. The
     *  previous hosted plugin (if any) is released on whichever thread holds
     *  the last reference. Returns true on success, false if the plugin
     *  cannot be prepared at the current SR / block size. */
    bool setHostedPlugin (std::unique_ptr<juce::AudioPluginInstance> plugin,
                          const juce::String& displayName);
    /** Drops the hosted plugin (next process() block runs NAM models again). */
    void clearHostedPlugin();
    bool hasHostedPlugin() const noexcept;
    juce::String getHostedPluginName() const;
    /** Raw pointer for opening the plugin editor on the UI thread. May be
     *  null. Do NOT delete -- the NAMProcessor owns it. */
    juce::AudioPluginInstance* getHostedPluginInstance() const noexcept;
    /** Filled-in description, or empty if no plugin is hosted. UI thread only. */
    juce::PluginDescription getHostedPluginDescription() const;

    /** Editor window for the hosted plugin -- UI-thread only. Stored here
     *  (rather than on MainComponent) so it survives popup dismissal and so
     *  the editor is automatically destroyed when the plugin is cleared. */
    std::unique_ptr<juce::DocumentWindow> hostedEditorWindow;

private:
    std::array<std::shared_ptr<nam::DSP>, kNumSlots> models;
    std::array<std::atomic<float>, kNumSlots>        weights;
    std::array<std::atomic<float>, kNumSlots>        normGainLinear;
    std::array<NAMModel,            kNumSlots>       slotMeta;
    std::array<std::atomic<float>, kNumSlots>        slimSize;
    std::array<std::atomic<bool>,  kNumSlots>        slotBypassed;
    std::array<std::atomic<float>, kNumSlots>        slotInputTrimDb;

    // Per-slot smoothed weight — ~20 ms equal-power crossfade on slot switch.
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kNumSlots> smoothedWeights;

    // Set by loadSlot(); consumed on the audio thread in process() to immediately
    // snap smoothers to the new weights rather than ramping from a stale 0.0
    // initial value (which made A/B/C silent at startup if prepare() ran first).
    std::atomic<bool> reinitSmoothedWeightsPending { false };

    std::atomic<float> preGainDb  { 0.0f };
    std::atomic<float> postGainDb { 0.0f };
    std::atomic<bool>  bypassed   { false };
    std::atomic<bool>  normalizeEnabled { true };
    std::atomic<int>   outputMode { 1 }; // 0=Raw, 1=Normalized, 2=Calibrated
    std::atomic<float> dryMakeupDb { 12.0f };
    std::atomic<float> xyBlendX   { 0.5f };
    std::atomic<float> xyBlendY   { 0.5f };

    juce::AudioBuffer<float> dryIn;
    juce::AudioBuffer<float> slotOut;
    juce::AudioBuffer<float> mixOut;

    // Oversampler — factor controlled by userOsMode + SR.
    // Lazy-initialised in prepare(); reset when oversampling is off.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    bool useOversampling { false };
    int  osExponent      { 0 };   // 0=1×, 1=2×, 2=4×
    std::atomic<int> userOsMode { (int) OsMode::Auto };
    // Hosted amp-sim plugin. Audio thread reads via std::atomic_load; UI thread
    // mutates via std::atomic_store. The shared_ptr's deleter calls
    // releaseResources() before destroying the underlying instance.
    std::shared_ptr<juce::AudioPluginInstance> hostedPlugin;
    juce::String                               hostedDisplayName;
    mutable juce::CriticalSection              hostedNameLock;
    juce::AudioBuffer<float>                   hostedStereo;
    juce::MidiBuffer                           hostedMidi;
    double sampleRate { 48000.0 };
    int    maxBlock   { 0 };
};
