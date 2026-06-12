#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Processors/InputProcessor.h"
#include "Processors/OutputProcessor.h"
#include "FX/NoiseGate.h"
#include "FX/EQ.h"
#include "FX/HitZoneDSP.h"
#include "FX/LevelDSP.h"
#include "FX/DoubleDSP.h"
#include "FX/AmpToneShaper.h"
#include "FX/Looper.h"
#include "FX/BackingTrackPlayer.h"
#include "FX/TransposeProcessor.h"
#include "NAM/NAMProcessor.h"
#include "Tuner/TunerProcessor.h"
#include "TempoClock.h"
#include "SpectrumTap.h"
#include "../PluginHost/PluginChain.h"

/** Top-level audio engine: Input -> Gate -> (pre-FX plugins) -> NAM -> EQ -> (post-FX plugins, includes user-loaded IR-loader plugin) -> Output.
 *
 *  The built-in IR convolver was removed: cabinet impulse responses are now
 *  loaded by dropping any third-party IR-loader plugin (Cab Lab, NadIR,
 *  MConvolutionEZ, ...) into the IR slot of the signal-chain bar, which
 *  routes into the post-FX plugin chain. This eliminates a redundant DSP
 *  stage and a category of phase/level issues that came from running both
 *  the built-in convolver AND a user IR plugin at the same time. */
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void start();
    void stop();

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;

    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    float getCpuUsage() const noexcept { return (float) deviceManager.getCpuUsage(); }

    InputProcessor&  getInput()  noexcept { return input;  }
    OutputProcessor& getOutput() noexcept { return output; }
    NoiseGate&       getGate()   noexcept { return gate;   }
    NAMProcessor&    getNAM()    noexcept { return nam;    }
    EQ&              getEQ()     noexcept { return eq;     }
    nhz::HitZoneDSP& getSweetSpot() noexcept { return sweetSpot; }
    na::LevelDSP&    getAutoLevel() noexcept { return autoLevel; }
    nd::DoubleDSP&   getDoubler()   noexcept { return doubler;   }
    nl::Looper&          getLooper()        noexcept { return looper;        }
    BackingTrackPlayer&   getBackingTrack()  noexcept { return backingTrack; }
    TransposeProcessor& getTranspose() noexcept { return transpose; }
    TunerProcessor&  getTuner()  noexcept { return tuner; }
    PluginChain&     getPreFxChain()  noexcept { return preFxChain;  }
    PluginChain&     getPostFxChain() noexcept { return postFxChain; }
    TempoClock&      getTempoClock()  noexcept { return tempoClock; }
    SpectrumTap&     getSpectrumTap() noexcept { return spectrumTap; }

    void  setDoublerWidth (float v) noexcept { doublerWidth.store (juce::jlimit (0.0f, 1.0f, v)); }
    void  setDoublerMix   (float v) noexcept { doublerMix  .store (juce::jlimit (0.0f, 1.0f, v)); }
    float getDoublerWidth() const noexcept   { return doublerWidth.load(); }
    float getDoublerMix()   const noexcept   { return doublerMix.load();   }

    void  setAutoLevelMacro (float v) noexcept { autoLevelMacro.store (juce::jlimit (0.0f, 1.0f, v)); }
    float getAutoLevelMacro() const noexcept   { return autoLevelMacro.load(); }
    void  setAutoLevelOn (bool b) noexcept { autoLevelOn.store (b); }
    bool  isAutoLevelOn() const noexcept   { return autoLevelOn.load(); }

    // Tight / Body / Air macros drive AmpToneShaper, a pre-NAM tone block.
    void  setTight (float v) noexcept { tightMacro.store (juce::jlimit (0.0f, 1.0f, v)); ampTone.setTight (v); }
    void  setBody  (float v) noexcept { bodyMacro .store (juce::jlimit (0.0f, 1.0f, v)); ampTone.setBody  (v); }
    void  setAir   (float v) noexcept { airMacro  .store (juce::jlimit (0.0f, 1.0f, v)); ampTone.setAir   (v); }
    float getTight() const noexcept { return tightMacro.load(); }
    float getBody()  const noexcept { return bodyMacro .load(); }
    float getAir()   const noexcept { return airMacro  .load(); }
    AmpToneShaper&   getAmpTone()   noexcept { return ampTone; }

    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    int    getCurrentBlockSize()  const noexcept { return currentBlockSize;  }

    /** Total latency in samples introduced by hosted plugins (pre + post chains)
     *  PLUS internal DSP latency from the NAM oversampler and the Transposer.
     *  Reported to the DAW so PDC stays sample-accurate when these stages are
     *  engaged. EQ / NoiseGate / AmpToneShaper / DC-blocker / safety limiter
     *  are all zero-latency by construction and don't appear here. */
    int    getTotalLatencySamples() const
    {
        return preFxChain.getReportedLatencySamples()
             + postFxChain.getReportedLatencySamples()
             + nam.getLatencySamples()
             + transpose.getLatencySamples();
    }

    /** Returns the number of audio-callback overruns since the last call,
     *  and atomically resets the counter to zero. Safe to call from the UI
     *  thread at any time. An overrun is defined as a callback that consumed
     *  more than 80 % of its nominal time budget. */
    int getAndResetGlitchCount() noexcept
    {
        return glitchCount.exchange (0);
    }

    //==========================================================================
    // Offline rendering
    //==========================================================================
    /** Stems captured during an offline (or future live) render pass. Any
     *  buffer left null/empty is skipped. All buffers must have at least
     *  numSamples allocated. */
    struct StemTaps
    {
        juce::AudioBuffer<float>* di       = nullptr;   // mono, post-input gain
        juce::AudioBuffer<float>* postNam  = nullptr;   // mono, post-amp
        juce::AudioBuffer<float>* postIr   = nullptr;   // stereo, post-cab pre-FX
        juce::AudioBuffer<float>* master   = nullptr;   // stereo, final output
        int                       offset   = 0;         // dest write offset
    };

    /** Prepare every DSP module for offline rendering at the given SR/block.
     *  Must be called from the MESSAGE thread after the live device has been
     *  stopped (call AudioEngine::stop() first). */
    void prepareOffline (double sampleRate, int blockSize);

    /** Run one block of DSP using the supplied mono input. Output goes to
     *  the stereo `outL/outR` pointers (each must hold at least numSamples).
     *  Optionally captures intermediate stems. Safe to call from a worker
     *  thread once `prepareOffline` has set the engine up and the live
     *  device is stopped. */
    void processOfflineBlock (const float* monoIn,
                              float* outL, float* outR,
                              int numSamples,
                              StemTaps* stems = nullptr);

    //==========================================================================
    // Plugin mode
    //==========================================================================
    /** Process one block in plugin mode (VST3 / CLAP).
     *  Reads mono guitar from channel 0 of `buffer`, runs the full signal
     *  chain, then writes stereo output back into the buffer (ch0 = L, ch1 = R).
     *  Must be called after prepareOffline(sampleRate, maxBlockSize). */
    void processPluginBlock (juce::AudioBuffer<float>& buffer,
                             juce::MidiBuffer&         midi);

private:
    juce::AudioDeviceManager deviceManager;

    InputProcessor  input;
    NoiseGate       gate;
    nhz::HitZoneDSP sweetSpot;
    TransposeProcessor transpose;
    AmpToneShaper   ampTone;
    NAMProcessor    nam;
    na::LevelDSP    autoLevel;
    EQ              eq;
    PluginChain     preFxChain;
    PluginChain     postFxChain;
    nd::DoubleDSP   doubler;
    nl::Looper          looper;
    BackingTrackPlayer   backingTrack;
    OutputProcessor output;
    TunerProcessor  tuner;
    TempoClock      tempoClock;
    SpectrumTap     spectrumTap;

    double currentSampleRate { 0.0 };
    int    currentBlockSize  { 0 };

    juce::AudioBuffer<float> monoWork;
    juce::AudioBuffer<float> stereoWork;

    std::atomic<float> doublerWidth { 0.0f };
    std::atomic<float> doublerMix   { 0.0f };
    std::atomic<float> autoLevelMacro { 0.5f };
    std::atomic<bool>  autoLevelOn    { false };
    std::atomic<float> tightMacro    { 0.0f };
    std::atomic<float> bodyMacro     { 0.5f };
    std::atomic<float> airMacro      { 0.5f };

    // Set true at the end of audioDeviceAboutToStart(), false in audioDeviceStopped().
    // The callback returns silence immediately if this is false, preventing any
    // DSP (including JACK) from running before all processors are fully prepared.
    // This also stops uncaught C++ exceptions from propagating into JACK's C
    // callback layer (which calls std::terminate and crashes the app).
    std::atomic<bool>  audioReady    { false };

    // Glitch / overrun counter. Incremented on the audio thread when a
    // callback exceeds 80 % of its time budget; read+reset from the UI thread.
    std::atomic<int>   glitchCount   { 0 };
    // juce::Time::getHighResolutionTicks() value at the *start* of the last
    // audio callback -- used to compute elapsed time vs. deadline.
    std::atomic<int64_t> lastCallbackTick { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
