#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class NeuralStageProcessor;
class MainComponent;

/** Plugin editor window that hosts the full NeuralStage MainComponent UI.
 *
 *  The window size is fixed to the same dimensions used by the standalone app
 *  so the UI renders at its intended 1:1 scale.  Hosts that support resizing
 *  can still resize via the standard AudioProcessorEditor resizing mechanism,
 *  but the default size matches the standalone. */
class NeuralStageEditor final : public juce::AudioProcessorEditor
{
public:
    explicit NeuralStageEditor (NeuralStageProcessor& proc);
    ~NeuralStageEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override {}

private:
    std::unique_ptr<MainComponent> mainComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralStageEditor)
};
