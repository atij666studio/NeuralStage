#pragma once
#include <juce_core/juce_core.h>

namespace ns::Logger
{
    inline void info  (const juce::String& m) { juce::Logger::writeToLog ("[NS][info]  " + m); }
    inline void warn  (const juce::String& m) { juce::Logger::writeToLog ("[NS][warn]  " + m); }
    inline void error (const juce::String& m) { juce::Logger::writeToLog ("[NS][error] " + m); }
}
