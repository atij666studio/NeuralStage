#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

/** Cab IR convolution loader.
 *
 *  Two-slot dual-IR design: each slot holds an independent mono convolution
 *  with its own pan position (-1 = full L, 0 = centre, +1 = full R) and per-
 *  slot enable. When only Slot A is loaded the output is identical to the
 *  legacy single-slot behaviour (mono-in / mono-out via process()).
 *
 *  When Slot B is also loaded (and pans differ from each other) the engine
 *  switches to processToStereo() to render a true-stereo cab — typical use
 *  case: SM57 panned 100%L, R121 panned 100%R, mixed at the same wet level.
 */
class IRLoader
{
public:
    enum Slot { SlotA = 0, SlotB = 1, kNumSlots = 2 };

    void prepare (double sampleRate, int blockSize);

    // ----- Mono in-place processing (single-IR / legacy path) ---------------
    void process (juce::AudioBuffer<float>& buffer);

    // ----- Stereo path (used when isStereoActive()) -------------------------
    /** Renders mono input through both slots, panning each to its position
     *  and summing into the stereo output buffer. monoIn must have 1 channel,
     *  stereoOut must have 2. Both must have at least numSamples samples. */
    void processToStereo (const juce::AudioBuffer<float>& monoIn,
                          juce::AudioBuffer<float>& stereoOut);

    /** True iff Slot B is loaded — the engine should call processToStereo()
     *  in that case instead of process() + manual mono->stereo upmix. */
    bool isStereoActive() const noexcept { return loaded[SlotB]; }

    // ----- Slot management --------------------------------------------------
    bool loadIR (Slot slot, const juce::File& wav);
    void clearIR (Slot slot);

    /** Backwards-compatible single-slot helpers (always operate on Slot A). */
    bool loadIR  (const juce::File& wav)  { return loadIR (SlotA, wav); }
    void clearIR()                         { clearIR (SlotA); clearIR (SlotB); }

    bool       isLoaded     () const noexcept            { return loaded[SlotA] || loaded[SlotB]; }
    bool       isLoaded     (Slot s) const noexcept      { return loaded[s]; }
    juce::File getCurrentFile() const noexcept           { return currentFile[SlotA]; }
    juce::File getCurrentFile(Slot s) const noexcept     { return currentFile[s]; }

    // Per-slot pan: -1..+1.
    void  setPan (Slot s, float pan01) noexcept { pan[s].store (juce::jlimit (-1.0f, 1.0f, pan01)); }
    float getPan (Slot s) const noexcept        { return pan[s].load(); }

    // Overall wet/dry mix (applied uniformly across both slots).
    void setMix (float wet01) noexcept { mix.store (juce::jlimit (0.0f, 1.0f, wet01)); }
    float getMix() const noexcept      { return mix.load(); }

    // Wet-bus makeup gain in dB. JUCE's Convolution::Normalise::yes
    // L2-normalises the IR so the convolved output is dramatically quieter
    // than the dry signal (especially for long cab IRs). +12 dB is a typical
    // compensation that brings a normalised cab IR back to roughly unity
    // perceived loudness; user can fine-tune per-IR via the UI.
    void  setMakeupDb (float db) noexcept { makeupDb.store (juce::jlimit (-24.0f, 24.0f, db)); }
    float getMakeupDb() const noexcept    { return makeupDb.load(); }

    /** Whole-cab bypass: when true, process() / processToStereo() pass the
     *  dry signal through (the latter still mirrors mono to L/R). Cheap
     *  atomic toggle — safe to drive from MIDI footswitch. */
    void setBypassed (bool b) noexcept { bypassed.store (b); }
    bool isBypassed() const noexcept   { return bypassed.load(); }

private:
    static void equalPowerPanGains (float pan, float& gL, float& gR) noexcept;

    std::array<juce::dsp::Convolution, kNumSlots> conv;
    std::array<bool, kNumSlots>                   loaded     { false, false };
    std::array<juce::File, kNumSlots>             currentFile;
    std::array<std::atomic<float>, kNumSlots>     pan        { { {-1.0f}, {1.0f} } }; // A=L, B=R defaults
    std::atomic<float>                            mix        { 1.0f };
    std::atomic<float>                            makeupDb   { 12.0f };
    std::atomic<bool>                             bypassed   { false };

    // Scratch buffers for stereo render (resized on prepare()).
    juce::AudioBuffer<float> wetA, wetB;

    bool   prepared { false };
    double sampleRate { 48000.0 };
    int    blockSize  { 0 };
};
