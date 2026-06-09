#pragma once
#include <juce_core/juce_core.h>

namespace ns::FileUtils
{
    inline juce::File userDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("NeuralStage");
    }

    /** Documents/NeuralStage — user-facing files (presets, scenes). */
    inline juce::File documentsDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("NeuralStage");
    }

    inline juce::File presetsDir() { return documentsDir().getChildFile ("Presets"); }
    inline juce::File namDir()     { return userDataDir() .getChildFile ("NAM");     }
    inline juce::File irDir()      { return userDataDir() .getChildFile ("IRs");     }

    inline juce::File audioDeviceSettingsFile()
    {
        return userDataDir().getChildFile ("AudioDeviceSettings.xml");
    }

    inline juce::File midiAssignmentsFile()
    {
        return userDataDir().getChildFile ("MidiAssignments.xml");
    }
}
