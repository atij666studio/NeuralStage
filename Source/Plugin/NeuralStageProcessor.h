#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class App;

/** Wraps the full NeuralStage engine as a VST3 / CLAP plug-in.
 *
 *  Bus layout:  1 input  (mono  — expects a mono guitar on channel 0)
 *               1 output (stereo)
 *
 *  All rig state (NAM models, FX chains, scenes, presets) is shared with the
 *  standalone NeuralStage.exe via the same user-data directory. DAW state
 *  save / restore is handled via getStateInformation / setStateInformation. */
class NeuralStageProcessor final : public juce::AudioProcessor
{
public:
    NeuralStageProcessor();
    ~NeuralStageProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NeuralStage"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Returns the latency in samples so the DAW can apply PDC. */
    int getLatencySamples() const;

private:
    // App is the NeuralStage state container (engine, presets, scenes, MIDI).
    // In plugin mode it is NOT a JUCEApplication; it is a plain singleton
    // owned here.  App::get() returns *app throughout the codebase.
    std::unique_ptr<App> app;

    // Deferred initialisation flag: pluginInitialise() is called on the first
    // prepareToPlay() so that juce_vst3_helper (which only needs the factory
    // interface, never calls prepareToPlay) can load the DLL without triggering
    // heavy init (file I/O, MIDI, chain loading) in its minimal JUCE context.
    bool appInitialised { false };

    // Lifetime sentinel for callAsync lambdas. Cleared in the destructor so
    // any queued warm-up lambdas bail safely if the processor is destroyed
    // before the message loop fires them.
    std::shared_ptr<bool> warmupAlive { std::make_shared<bool> (true) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralStageProcessor)
};
