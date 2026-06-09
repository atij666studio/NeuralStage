#include "NeuralStageEditor.h"
#include "NeuralStageProcessor.h"
#include "../UI/MainComponent.h"
#include "../UI/Styles/UIConstants.h"

NeuralStageEditor::NeuralStageEditor (NeuralStageProcessor& proc)
    : AudioProcessorEditor (proc)
{
    mainComponent = std::make_unique<MainComponent>();
    addAndMakeVisible (*mainComponent);

    // Match the standalone window's design size so the UI renders at 1:1.
    // MainComponent calls setSize() on itself, but we mirror that here so
    // the DAW's plugin window is sized correctly on first open.
    setSize (ns::UI::kAppWidth, ns::UI::kAppHeight);
    setResizable (true, false);
    setResizeLimits (ns::UI::kAppWidth, ns::UI::kAppHeight, 3840, 2400);
}

NeuralStageEditor::~NeuralStageEditor() = default;

void NeuralStageEditor::resized()
{
    if (mainComponent)
        mainComponent->setBounds (getLocalBounds());
}
