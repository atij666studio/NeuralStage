#include "MIDIManager.h"

void MIDIManager::start (juce::AudioDeviceManager& dm)
{
    refresh (dm);
}

void MIDIManager::refresh (juce::AudioDeviceManager& dm)
{
    // Remove first to avoid double-registration (which would deliver every
    // message twice), then re-enable the input and re-attach our callback.
    for (auto& dev : juce::MidiInput::getAvailableDevices())
    {
        dm.removeMidiInputDeviceCallback (dev.identifier, this);
        dm.setMidiInputDeviceEnabled (dev.identifier, true);
        dm.addMidiInputDeviceCallback (dev.identifier, this);
    }
}

void MIDIManager::stop (juce::AudioDeviceManager& dm)
{
    for (auto& dev : juce::MidiInput::getAvailableDevices())
        dm.removeMidiInputDeviceCallback (dev.identifier, this);
}

void MIDIManager::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg)
{
    if (registry != nullptr) registry->handleMidi (msg);
    if (msg.isProgramChange() && onProgramChange)
        onProgramChange (msg.getProgramChangeNumber());
    if (onMessage) onMessage (msg);
}
