#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

/** Built-in noise gate.
 *
 *  Sits BEFORE the pre-FX chain (in AudioEngine: gate runs right after input
 *  gain, before any user-loaded plugins). Enabled flag controls whether
 *  it gates at all; when off the buffer passes through untouched.
 *
 *  Envelope follower: per-sample peak detection (max of |L|, |R|) with
 *  asymmetric one-pole smoothing. Attack / release are in ms; coefficients
 *  are recomputed in prepare() and again whenever the user changes the ms
 *  values (lock-free: the coefficient atomics are written from the audio
 *  thread when it notices a parameter change). Hold (ms) prevents the gate
 *  from chattering on signals that hover near the threshold -- the gate
 *  stays open for at least `holdMs` after the last sample above threshold.
 */
class NoiseGate
{
public:
    void prepare (double sampleRate, int blockSize);
    void process (juce::AudioBuffer<float>& buffer);

    // Threshold (-80..0 dB). Below = gate closed.
    void  setThresholdDb (float db) noexcept { thresholdDb.store (db); }
    float getThresholdDb() const noexcept    { return thresholdDb.load(); }

    // Attack (0.1..50 ms). How fast the gain opens once signal crosses threshold.
    void  setAttackMs    (float ms) noexcept { attackMs.store (juce::jlimit (0.1f, 50.0f, ms)); coefsDirty.store (true); }
    float getAttackMs()    const noexcept    { return attackMs.load(); }

    // Release (5..2000 ms). How slowly the gain closes after signal drops.
    void  setReleaseMs   (float ms) noexcept { releaseMs.store (juce::jlimit (5.0f, 2000.0f, ms)); coefsDirty.store (true); }
    float getReleaseMs()   const noexcept    { return releaseMs.load(); }

    // Hold (0..500 ms). Minimum time the gate stays open after the last
    // above-threshold sample -- suppresses chatter on borderline signals.
    void  setHoldMs      (float ms) noexcept { holdMs.store (juce::jlimit (0.0f, 500.0f, ms)); }
    float getHoldMs()      const noexcept    { return holdMs.load(); }

    void setEnabled (bool b) noexcept { enabled.store (b); }
    bool isEnabled() const noexcept   { return enabled.load(); }

private:
    void updateCoefficients (double sr) noexcept;

    std::atomic<float> thresholdDb { -60.0f };
    std::atomic<float> attackMs    {  3.0f };
    std::atomic<float> releaseMs   { 120.0f };
    std::atomic<float> holdMs      {  20.0f };
    std::atomic<bool>  enabled     { false };
    std::atomic<bool>  coefsDirty  { true };

    // Cached coefficients (one-pole exp time constants). Audio-thread only.
    float attackCoef  { 0.99f };
    float releaseCoef { 0.995f };

    float  envelope     { 0.0f };
    int    holdSamples  { 0 };
    int    holdCounter  { 0 };
    double currentSR    { 48000.0 };
};
