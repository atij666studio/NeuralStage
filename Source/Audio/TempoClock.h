#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

/** Global tempo source for the rig. Provides:
 *   - Tap-tempo input (median-of-recent-intervals BPM detection)
 *   - A juce::AudioPlayHead implementation that any hosted plugin can read
 *     (delays, modulation, etc.) so they can sync to "host" BPM.
 *
 *  Thread model:
 *   - tap() / setBpm() are message-thread.
 *   - advance() is audio-thread (called once per processed block by AudioEngine).
 *   - The PlayHead is read on the audio thread (plugins query it inside processBlock).
 *  All shared state is in std::atomic so locking is unnecessary.
 */
class TempoClock : public juce::AudioPlayHead
{
public:
    TempoClock() = default;

    // ---- Tap tempo ----
    /** Register a tap event. Computes BPM from the median of the last few
     *  inter-tap intervals (window <= 2 s). A single tap arms; >=2 sets BPM. */
    void tap();

    /** Manually set BPM (e.g. from a MIDI CC or text input). Clamped 30..300. */
    void setBpm (double newBpm) noexcept;
    double getBpm() const noexcept { return bpm.load(); }

    /** True briefly after a tap (UI flash). */
    bool justTapped() noexcept
    {
        const auto t = juce::Time::currentTimeMillis();
        return (t - lastTapMs.load()) < 200;
    }

    // ---- Audio-thread driver ----
    void prepare (double sr) noexcept;
    void advance (int numSamples) noexcept;

    // ---- juce::AudioPlayHead ----
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;

private:
    std::atomic<double> bpm           { 120.0 };
    std::atomic<double> sampleRate    { 48000.0 };
    std::atomic<double> ppqPosition   { 0.0 };
    std::atomic<double> timeInSeconds { 0.0 };
    std::atomic<juce::int64> lastTapMs { 0 };

    // Tap-tempo intervals (ms), most-recent-last. Touched only on message thread.
    juce::Array<juce::int64> tapTimesMs;
};
