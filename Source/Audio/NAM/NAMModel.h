#pragma once
#include <juce_core/juce_core.h>

/** Plain-data description of a NAM model on disk. */
struct NAMModel
{
    juce::File        filePath;
    double            sampleRate { 48000.0 };
    float             inputGain  { 0.0f };   // dB
    float             outputGain { 0.0f };   // dB
    juce::StringArray tags;                  // e.g. "clean", "high gain"
    juce::String      displayName;
};
