#include "TempoClock.h"

void TempoClock::tap()
{
    const auto now = juce::Time::currentTimeMillis();
    lastTapMs.store (now);

    // Drop taps older than 2 s -- prevents stale window contaminating BPM.
    while (tapTimesMs.size() > 0 && (now - tapTimesMs.getFirst()) > 2000)
        tapTimesMs.remove (0);

    tapTimesMs.add (now);
    if (tapTimesMs.size() > 8) tapTimesMs.remove (0);

    if (tapTimesMs.size() < 2) return;

    juce::Array<juce::int64> intervals;
    for (int i = 1; i < tapTimesMs.size(); ++i)
        intervals.add (tapTimesMs[i] - tapTimesMs[i - 1]);

    intervals.sort();
    const auto medianMs = intervals[intervals.size() / 2];
    if (medianMs <= 0) return;

    setBpm (60000.0 / (double) medianMs);
}

void TempoClock::setBpm (double newBpm) noexcept
{
    bpm.store (juce::jlimit (30.0, 300.0, newBpm));
}

void TempoClock::prepare (double sr) noexcept
{
    sampleRate.store (sr > 0 ? sr : 48000.0);
    ppqPosition.store (0.0);
    timeInSeconds.store (0.0);
}

void TempoClock::advance (int numSamples) noexcept
{
    const double sr  = sampleRate.load();
    const double sec = (double) numSamples / sr;
    timeInSeconds.store (timeInSeconds.load() + sec);

    const double quartersPerSec = bpm.load() / 60.0;
    ppqPosition.store (ppqPosition.load() + sec * quartersPerSec);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> TempoClock::getPosition() const
{
    juce::AudioPlayHead::PositionInfo info;
    info.setBpm                    (bpm.load());
    info.setIsPlaying              (true);
    info.setIsRecording            (false);
    info.setIsLooping              (false);
    info.setTimeInSamples          ((juce::int64) (timeInSeconds.load() * sampleRate.load()));
    info.setTimeInSeconds          (timeInSeconds.load());
    info.setPpqPosition            (ppqPosition.load());
    info.setPpqPositionOfLastBarStart (std::floor (ppqPosition.load() / 4.0) * 4.0);
    info.setTimeSignature          (juce::AudioPlayHead::TimeSignature{ 4, 4 });
    return info;
}
