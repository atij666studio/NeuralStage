#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

/** Input stage: applies pre-gain and (later) input-level calibration
 *  (measure → adjust gain → target level).
 */
class InputProcessor
{
public:
    void prepare (double sampleRate, int blockSize);
    void process (juce::AudioBuffer<float>& buffer);

    void  setPreGainDb (float db) noexcept { preGainDb.store (db); }
    float getPreGainDb() const noexcept    { return preGainDb.load(); }
    float getMeasuredLevel() const noexcept { return measured.load(); }

    void setMute (bool m) noexcept { muted.store (m); }
    bool isMuted() const noexcept  { return muted.load(); }

private:
    std::atomic<float> preGainDb { 0.0f };
    std::atomic<float> measured  { 0.0f };
    std::atomic<bool>  muted     { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain { 1.0f };

    // Per-channel 1-pole DC-blocker (HPF ~20 Hz). Removes DC offset / sub-20
    // Hz pickup rumble that otherwise eats NAM headroom and modulates the
    // amp model's bias point. Standard pro-amp input stage hygiene.
    static constexpr int kMaxInCh = 2;
    float dcX1[kMaxInCh] { 0.0f, 0.0f };
    float dcY1[kMaxInCh] { 0.0f, 0.0f };
    float dcR { 0.9995f }; // recomputed in prepare() from sample rate
};
