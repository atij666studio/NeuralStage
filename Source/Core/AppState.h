#pragma once
#include <juce_data_structures/juce_data_structures.h>

/** Central observable application state (preset, scene index, etc.). */
class AppState
{
public:
    juce::ValueTree state { "NeuralStage" };

    AppState();
};
