#include "MidiClockSender.h"

juce::StringArray MidiClockSender::availableOutputs()
{
    juce::StringArray names;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        names.add (d.name);
    return names;
}

bool MidiClockSender::setOutputByName (const juce::String& nameSubstring)
{
    const bool wasEnabled = enabled.load();
    if (wasEnabled) setEnabled (false);

    output.reset();
    openedName.clear();

    auto devs = juce::MidiOutput::getAvailableDevices();
    if (devs.isEmpty()) return false;

    juce::MidiDeviceInfo chosen = devs.getFirst();
    if (nameSubstring.isNotEmpty())
        for (auto& d : devs)
            if (d.name.containsIgnoreCase (nameSubstring))
                { chosen = d; break; }

    output = juce::MidiOutput::openDevice (chosen.identifier);
    if (output != nullptr) openedName = chosen.name;

    if (wasEnabled) setEnabled (true);
    return output != nullptr;
}

void MidiClockSender::setEnabled (bool b)
{
    if (b == enabled.load()) return;

    if (b)
    {
        if (output == nullptr && ! setOutputByName ({}))
            return;                       // no MIDI outputs at all

        // Send 0xFA Start, then begin pulses.
        output->sendMessageNow (juce::MidiMessage::midiStart());
        enabled.store (true);
        restartTimerIfRunning();
    }
    else
    {
        stopTimer();
        enabled.store (false);
        if (output != nullptr)
            output->sendMessageNow (juce::MidiMessage::midiStop());
    }
}

void MidiClockSender::restartTimerIfRunning()
{
    if (! enabled.load()) return;
    // 24 ticks per quarter note. interval = 60_000 / (BPM * 24) ms.
    const double bpm = currentBpm.load();
    const int    intervalMs = juce::jmax (1, (int) std::round (60000.0 / (bpm * 24.0)));
    startTimer (intervalMs);
}

void MidiClockSender::hiResTimerCallback()
{
    if (output != nullptr)
        output->sendMessageNow (juce::MidiMessage::midiClock());
}

void MidiClockSender::sendPanic()
{
    if (output == nullptr && ! setOutputByName ({}))
        return;
    if (output == nullptr) return;
    for (int ch = 1; ch <= 16; ++ch)
    {
        output->sendMessageNow (juce::MidiMessage::controllerEvent (ch, 120, 0));
        output->sendMessageNow (juce::MidiMessage::controllerEvent (ch, 123, 0));
        output->sendMessageNow (juce::MidiMessage::controllerEvent (ch,  64, 0));
        output->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
    }
}
