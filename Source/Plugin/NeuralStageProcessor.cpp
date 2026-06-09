#include "../App.h"
#include "NeuralStageProcessor.h"
#include "NeuralStageEditor.h"

NeuralStageProcessor::NeuralStageProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::mono(),   true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Create the App singleton now so App::get() is valid, but defer the
    // heavy pluginInitialise() (file I/O, MIDI, chain loading) until the
    // first prepareToPlay() call.  This lets juce_vst3_helper load the DLL
    // for factory scanning without triggering anything that needs a running
    // DAW host environment.
    app = std::make_unique<App>();
}

NeuralStageProcessor::~NeuralStageProcessor()
{
    // Signal any pending callAsync warmup lambdas to bail before we tear down.
    if (warmupAlive) *warmupAlive = false;
    if (appInitialised)
        app->pluginShutdown();
    app.reset();   // clears App::s_instance in App destructor
}

//==============================================================================
void NeuralStageProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // First call from the DAW: run the deferred heavy initialisation
    // (managers, MIDI registration, file restore).  Safe here because the
    // message thread and JUCE event infrastructure are fully running.
    if (! appInitialised)
    {
        app->pluginInitialise();
        appInitialised = true;
    }

    // Reuse the offline-render prepare path — identical to audioDeviceAboutToStart
    // but without the device pointer.
    App::get().getAudioEngine().prepareOffline (sampleRate, samplesPerBlock);

    // Report latency so the DAW can apply PDC.
    setLatencySamples (getLatencySamples());
}

void NeuralStageProcessor::releaseResources()
{
    // Nothing to tear down — chains and processors are reset on the next
    // prepareToPlay call.
}

bool NeuralStageProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Output must be stereo; input can be mono or stereo (we always read ch0).
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    auto in = layouts.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::mono() &&
        in != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void NeuralStageProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi)
{
    App::get().getAudioEngine().processPluginBlock (buffer, midi);
}

int NeuralStageProcessor::getLatencySamples() const
{
    return App::get().getAudioEngine().getTotalLatencySamples();
}

//==============================================================================
juce::AudioProcessorEditor* NeuralStageProcessor::createEditor()
{
    // Some DAWs open the editor before the first prepareToPlay() call (e.g.
    // when auto-opening the plugin window after insert).  Ensure the app
    // state (managers, MIDI, etc.) is set up so the UI can safely call
    // App::get().getPresetManager() / getSceneManager() etc.
    if (! appInitialised)
    {
        app->pluginInitialise();
        appInitialised = true;
    }

    return new NeuralStageEditor (*this);
}

//==============================================================================
void NeuralStageProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (app == nullptr || ! appInitialised) return;
    auto& pm = App::get().getPresetManager();
    auto state = pm.captureState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NeuralStageProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (app == nullptr || ! appInitialised) return;
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto vt = juce::ValueTree::fromXml (*xml);
        if (vt.isValid())
        {
            App::get().getPresetManager().restoreState (vt);
            // Schedule warm-up via callAsync so it fires AFTER the DAW releases
            // its plugin-loading locks (which are held during setStateInformation).
            // The warmupAlive sentinel prevents dangling access if the processor
            // is destroyed before the message loop fires this lambda.
            std::weak_ptr<bool> wkAlive = warmupAlive;
            juce::MessageManager::callAsync ([wkAlive]
            {
                if (auto alive = wkAlive.lock(); alive && *alive)
                    App::get().pluginWarmUp();
            });
        }
    }
}

//==============================================================================
// JUCE plugin entry point — called by the host when loading the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeuralStageProcessor();
}
