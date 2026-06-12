#include "AudioEngine.h"
#include <stdexcept>

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::start()
{
    deviceManager.initialiseWithDefaultDevices (1, 2);
    deviceManager.addAudioCallback (this);
}

void AudioEngine::stop()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    currentBlockSize  = device->getCurrentBufferSizeSamples();

    monoWork  .setSize (1, currentBlockSize, false, false, true);
    stereoWork.setSize (2, currentBlockSize, false, false, true);

    input .prepare (currentSampleRate, currentBlockSize);
    gate  .prepare (currentSampleRate, currentBlockSize);
    sweetSpot.prepare (currentSampleRate, currentBlockSize, 1);
    transpose.prepare (currentSampleRate, currentBlockSize);
    ampTone  .prepare (currentSampleRate, currentBlockSize);
    preFxChain .prepare (currentSampleRate, currentBlockSize, 1);
    nam   .prepare (currentSampleRate, currentBlockSize);
    autoLevel.prepare (currentSampleRate, currentBlockSize, 1);
    eq    .prepare (currentSampleRate, currentBlockSize);
    postFxChain.prepare (currentSampleRate, currentBlockSize, 2);
    doubler.prepare (currentSampleRate, currentBlockSize, 2);
    looper      .prepare (currentSampleRate, currentBlockSize, 2);
    backingTrack.prepare (currentSampleRate, currentBlockSize);
    output.prepare (currentSampleRate, currentBlockSize);
    tuner .prepare (currentSampleRate, currentBlockSize);

    tempoClock.prepare (currentSampleRate);
    preFxChain .setHostPlayHead (&tempoClock);
    postFxChain.setHostPlayHead (&tempoClock);

    spectrumTap.prepare (currentSampleRate);

    // Mark the engine ready AFTER every processor has been prepared.
    // The callback checks this flag and returns silence until it is set.
    audioReady.store (true);
}

void AudioEngine::audioDeviceStopped()
{
    audioReady.store (false);
    currentSampleRate = 0.0;
    currentBlockSize  = 0;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // Guard: if audioDeviceAboutToStart hasn't completed (or we're tearing down),
    // return silence. On JACK this prevents the first callbacks from running DSP
    // before all processors are prepared, and stops any C++ exception from
    // propagating into JACK's C callback layer (which calls std::terminate).
    if (! audioReady.load (std::memory_order_acquire))
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        return;
    }

    // Wrap everything in try/catch: JACK's process callback is a C ABI
    // boundary — a propagating C++ exception would call std::terminate and
    // crash the app (the XRun-flood crash observed on Patchbox OS / Pi 5).
    try
    {

    // Pro-grade housekeeping: enable FTZ/DAZ for the duration of this
    // callback so subnormal numbers (which can otherwise hit the FPU and
    // create CPU spikes / crackles inside IIR tails, reverbs, plug-ins) are
    // flushed to zero by the hardware. Restored on scope exit.
    const juce::ScopedNoDenormals noDenormals;

    // --- Glitch / overrun detector ---
    // Budget = numSamples / sampleRate seconds (the nominal inter-callback
    // interval). If the gap since the last callback START exceeds 1.5× that
    // budget the previous callback almost certainly overran -- the OS had to
    // wait more than half a block's extra time before firing us again.
    // Using 0.8× (the old threshold) fired on every healthy callback because
    // the normal gap is ≈1.0× budget, which is always > 0.8×.
    {
        const int64_t now   = juce::Time::getHighResolutionTicks();
        const int64_t prev  = lastCallbackTick.exchange (now);
        if (prev != 0 && currentSampleRate > 0.0 && numSamples > 0)
        {
            const double budget_ticks = (numSamples / currentSampleRate)
                                        * juce::Time::getHighResolutionTicksPerSecond();
            if ((now - prev) > static_cast<int64_t> (budget_ticks * 1.5))
                glitchCount.fetch_add (1, std::memory_order_relaxed);
        }
    }

    // Defensive resize if the host suddenly enlarges the block.
    if (monoWork.getNumSamples()   < numSamples) monoWork  .setSize (1, numSamples, false, false, true);
    if (stereoWork.getNumSamples() < numSamples) stereoWork.setSize (2, numSamples, false, false, true);

    // Mono input.
    const float* in = (numInputChannels > 0 && inputChannelData[0] != nullptr)
                      ? inputChannelData[0]
                      : nullptr;

    juce::AudioBuffer<float> mono (monoWork.getArrayOfWritePointers(), 1, numSamples);
    if (in != nullptr) mono.copyFrom (0, 0, in, numSamples);
    else               mono.clear();

    // Mono amp/cab/EQ stages.
    input.process (mono);
    tuner.pushBlock (mono);             // non-destructive tap (post-input gain)
    gate .process (mono);
    sweetSpot.process (mono);           // input conditioning
    transpose.process (mono);           // pitch shift before NAM
    ampTone  .process (mono);           // TIGHT/BODY/AIR tone shaping pre-NAM
    preFxChain.process (mono);          // Gate-/Drive-category plugins (pre-amp)
    nam  .process (mono);

    // ---- autoLevel + EQ + mono->stereo upmix ----
    // Cab IRs are now handled by a user-loaded IR-loader plugin in the
    // post-FX chain (IRLoader category), so there is no built-in convolver
    // here. We just finalise the mono signal, then upmix to stereo.
    juce::AudioBuffer<float> st (stereoWork.getArrayOfWritePointers(), 2, numSamples);

    autoLevel.process (mono, autoLevelMacro.load(), autoLevelOn.load(), 0.0f);
    eq   .process (mono);
    juce::FloatVectorOperations::copy (st.getWritePointer (0), mono.getReadPointer (0), numSamples);
    juce::FloatVectorOperations::copy (st.getWritePointer (1), mono.getReadPointer (0), numSamples);

    doubler.process (st, doublerWidth.load(), doublerMix.load(), 0.0f);
    postFxChain.process (st);           // FX-category plugins (post-amp, stereo)
    looper      .setBpm (tempoClock.getBpm()); // keep metronome/count-in in sync with global tempo
    looper      .process (st);          // master-bus looper (records/overdubs the wet mix)
    backingTrack.process (st);          // backing track mixed in post-FX, pre-output
    output      .process (st);

    // Spectrum analyser tap (lock-free, mono sum of final output).
    spectrumTap.pushStereo (st);

    // Advance the global tempo PlayHead by exactly the samples we just
    // produced, so any tempo-syncing plugin (delay/mod) stays in step.
    tempoClock.advance (numSamples);

    // Write to device outputs.
    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr) continue;
        const int srcCh = juce::jmin (ch, st.getNumChannels() - 1);
        juce::FloatVectorOperations::copy (outputChannelData[ch], st.getReadPointer (srcCh), numSamples);
    }

    } // end try
    catch (const std::exception& e)
    {
        // An exception inside the audio callback would propagate into JACK's C
        // ABI and call std::terminate (crash + XRun flood). Catch it, output
        // silence, and log — the device will keep running and the user can
        // switch away without the app dying.
        juce::Logger::writeToLog (juce::String ("AudioEngine callback exception: ") + e.what());
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
    }
    catch (...)
    {
        juce::Logger::writeToLog ("AudioEngine callback: unknown exception caught");
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
    }
}

//==============================================================================
// Offline rendering
//==============================================================================
void AudioEngine::prepareOffline (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;

    monoWork  .setSize (1, blockSize, false, false, true);
    stereoWork.setSize (2, blockSize, false, false, true);

    input .prepare (sampleRate, blockSize);
    gate  .prepare (sampleRate, blockSize);
    sweetSpot.prepare (sampleRate, blockSize, 1);
    transpose.prepare (sampleRate, blockSize);
    ampTone  .prepare (sampleRate, blockSize);
    preFxChain .prepare (sampleRate, blockSize, 1);
    nam   .prepare (sampleRate, blockSize);
    autoLevel.prepare (sampleRate, blockSize, 1);
    eq    .prepare (sampleRate, blockSize);
    postFxChain.prepare (sampleRate, blockSize, 2);
    doubler.prepare (sampleRate, blockSize, 2);
    looper      .prepare (sampleRate, blockSize, 2);
    backingTrack.prepare (sampleRate, blockSize);
    output.prepare (sampleRate, blockSize);
    tuner .prepare (sampleRate, blockSize);

    tempoClock.prepare (sampleRate);
    preFxChain .setHostPlayHead (&tempoClock);
    postFxChain.setHostPlayHead (&tempoClock);
}

void AudioEngine::processOfflineBlock (const float* monoIn,
                                       float* outL, float* outR,
                                       int numSamples,
                                       StemTaps* stems)
{
    const juce::ScopedNoDenormals noDenormals;

    if (monoWork.getNumSamples()   < numSamples) monoWork  .setSize (1, numSamples, false, false, true);
    if (stereoWork.getNumSamples() < numSamples) stereoWork.setSize (2, numSamples, false, false, true);

    juce::AudioBuffer<float> mono (monoWork.getArrayOfWritePointers(), 1, numSamples);
    if (monoIn != nullptr) mono.copyFrom (0, 0, monoIn, numSamples);
    else                   mono.clear();

    input.process (mono);

    if (stems != nullptr && stems->di != nullptr)
        stems->di->copyFrom (0, stems->offset, mono, 0, 0, numSamples);

    tuner.pushBlock (mono);
    gate .process (mono);
    sweetSpot.process (mono);
    transpose.process (mono);
    ampTone  .process (mono);
    preFxChain.process (mono);
    nam  .process (mono);

    if (stems != nullptr && stems->postNam != nullptr)
        stems->postNam->copyFrom (0, stems->offset, mono, 0, 0, numSamples);

    juce::AudioBuffer<float> st (stereoWork.getArrayOfWritePointers(), 2, numSamples);

    autoLevel.process (mono, autoLevelMacro.load(), autoLevelOn.load(), 0.0f);
    eq   .process (mono);
    juce::FloatVectorOperations::copy (st.getWritePointer (0), mono.getReadPointer (0), numSamples);
    juce::FloatVectorOperations::copy (st.getWritePointer (1), mono.getReadPointer (0), numSamples);

    if (stems != nullptr && stems->postIr != nullptr)
    {
        stems->postIr->copyFrom (0, stems->offset, st, 0, 0, numSamples);
        stems->postIr->copyFrom (1, stems->offset, st, 1, 0, numSamples);
    }

    doubler.process (st, doublerWidth.load(), doublerMix.load(), 0.0f);
    postFxChain.process (st);
    looper      .process (st);
    backingTrack.process (st);
    output      .process (st);

    tempoClock.advance (numSamples);

    if (stems != nullptr && stems->master != nullptr)
    {
        stems->master->copyFrom (0, stems->offset, st, 0, 0, numSamples);
        stems->master->copyFrom (1, stems->offset, st, 1, 0, numSamples);
    }

    if (outL != nullptr)
        juce::FloatVectorOperations::copy (outL, st.getReadPointer (0), numSamples);
    if (outR != nullptr)
        juce::FloatVectorOperations::copy (outR, st.getReadPointer (1), numSamples);
}

//==============================================================================
// Plugin-mode block processing
//==============================================================================
void AudioEngine::processPluginBlock (juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& /*midi*/)
{
    const int n  = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    if (n <= 0) return;

    const juce::ScopedNoDenormals noDenormals;

    if (monoWork.getNumSamples()   < n) monoWork  .setSize (1, n, false, false, true);
    if (stereoWork.getNumSamples() < n) stereoWork.setSize (2, n, false, false, true);

    // Read mono guitar from channel 0 (channel 1 is ignored; NeuralStage is
    // fundamentally a mono-input signal chain).
    juce::AudioBuffer<float> mono (monoWork.getArrayOfWritePointers(), 1, n);
    if (nc > 0) mono.copyFrom (0, 0, buffer, 0, 0, n);
    else        mono.clear();

    // Full signal chain — identical to the live device callback.
    input.process (mono);
    tuner.pushBlock (mono);
    gate .process (mono);
    sweetSpot.process (mono);
    transpose.process (mono);
    ampTone  .process (mono);
    preFxChain.process (mono);
    nam  .process (mono);

    juce::AudioBuffer<float> st (stereoWork.getArrayOfWritePointers(), 2, n);
    autoLevel.process (mono, autoLevelMacro.load(), autoLevelOn.load(), 0.0f);
    eq   .process (mono);
    juce::FloatVectorOperations::copy (st.getWritePointer (0), mono.getReadPointer (0), n);
    juce::FloatVectorOperations::copy (st.getWritePointer (1), mono.getReadPointer (0), n);

    doubler.process (st, doublerWidth.load(), doublerMix.load(), 0.0f);
    postFxChain.process (st);
    looper      .setBpm (tempoClock.getBpm());
    looper      .process (st);
    backingTrack.process (st);
    output      .process (st);

    spectrumTap.pushStereo (st);
    tempoClock.advance (n);

    // Write stereo result back into the DAW buffer.
    for (int ch = 0; ch < nc; ++ch)
    {
        const int srcCh = juce::jmin (ch, st.getNumChannels() - 1);
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch),
                                           st.getReadPointer (srcCh), n);
    }
}
