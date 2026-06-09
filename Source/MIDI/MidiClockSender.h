#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

/** Sends MIDI Clock (24 PPQN) + Start/Stop to a chosen MIDI output device.
 *  Off by default. The clock thread is a juce::HighResolutionTimer so the
 *  pulse interval is sub-millisecond accurate even at 300 BPM (~8.3 ms / tick).
 *
 *  Usage:
 *    sender.setOutputByName ("UM-ONE");   // or empty to use first available
 *    sender.setBpm (120.0);
 *    sender.setEnabled (true);            // sends 0xFA Start, then ticks
 *    sender.setBpm (140.0);               // adjust on the fly
 *    sender.setEnabled (false);           // sends 0xFC Stop and halts
 */
class MidiClockSender : private juce::HighResolutionTimer
{
public:
    MidiClockSender() = default;
    ~MidiClockSender() override { setEnabled (false); }

    /** Open a MIDI output by name match (substring, case-insensitive). Pass
     *  empty string to pick the first available output. Returns true if an
     *  output was opened. Closes the previous one. Message thread. */
    bool setOutputByName (const juce::String& nameSubstring);

    /** Currently opened output device's display name, or "" if none. */
    juce::String getOutputName() const noexcept { return openedName; }

    /** Available MIDI output device display names. Message thread. */
    static juce::StringArray availableOutputs();

    void setBpm (double bpm) noexcept { currentBpm.store (juce::jlimit (30.0, 300.0, bpm)); restartTimerIfRunning(); }
    double getBpm() const noexcept    { return currentBpm.load(); }

    void setEnabled (bool b);
    bool isEnabled() const noexcept   { return enabled.load(); }

    /** Send MIDI Panic out the currently-opened output (if any): CC120, CC123,
     *  CC64=0 on all 16 channels. Safe to call from the message thread. */
    void sendPanic();

private:
    void hiResTimerCallback() override;
    void restartTimerIfRunning();

    std::unique_ptr<juce::MidiOutput> output;
    juce::String openedName;
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<bool>   enabled    { false };
};
