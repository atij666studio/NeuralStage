#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "MIDILearn.h"

class MIDIManager : public juce::MidiInputCallback
{
public:
    void start (juce::AudioDeviceManager& dm);
    void stop  (juce::AudioDeviceManager& dm);

    /** Re-enable every available MIDI input and (re)attach our callback.
     *  Safe to call repeatedly. Used after the Audio/MIDI Settings dialog
     *  closes (its AudioDeviceSelectorComponent can leave inputs disabled or
     *  detach our callback) and to pick up controllers hot-plugged after boot. */
    void refresh (juce::AudioDeviceManager& dm);

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void setLearnRegistry (MIDILearnRegistry* reg) noexcept { registry = reg; }

    std::function<void (const juce::MidiMessage&)> onMessage;

    /** Optional callback fired on every Program Change (audio thread).
     *  Argument is the program number 0..127. */
    std::function<void (int)> onProgramChange;

private:
    MIDILearnRegistry* registry { nullptr };
};
