#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>

class AudioEngine;

/** Offline (faster-than-real-time) renderer that drives the live AudioEngine
 *  in non-realtime mode to bounce an input WAV through the current rig and
 *  write up to four stem WAVs into an output folder.
 *
 *  Designed to be run on a background ThreadWithProgressWindow so the UI
 *  stays responsive. The live audio device must be stopped for the duration
 *  of the render (the constructor takes care of that).
 *
 *  Stems written:
 *    - <base>_DI.wav        mono   - post-input gain, pre-everything else
 *    - <base>_PostNAM.wav   mono   - post-amp, pre-cab
 *    - <base>_PostIR.wav    stereo - post-cab, pre-FX (the "cab feed")
 *    - <base>_Master.wav    stereo - the final stereo output
 *
 *  The user can disable individual stems via the captureMask. The master
 *  stem is always recommended for a single-file bounce.
 */
class OfflineRenderer : public juce::ThreadWithProgressWindow
{
public:
    enum StemFlags
    {
        StemDI      = 1 << 0,
        StemPostNAM = 1 << 1,
        StemPostIR  = 1 << 2,
        StemMaster  = 1 << 3,
        StemAll     = StemDI | StemPostNAM | StemPostIR | StemMaster
    };

    OfflineRenderer (AudioEngine& engine,
                     const juce::File& inputWav,
                     const juce::File& outputDir,
                     const juce::String& outputBaseName,
                     int captureMask,
                     int bitsPerSample = 24);

    /** Result message after `run()`/`runThread()` completes. */
    const juce::String& getResultMessage() const noexcept { return resultMessage; }
    bool                wasSuccessful()    const noexcept { return success;       }

private:
    void run() override;

    bool writeStem (const juce::File& dest,
                    const juce::AudioBuffer<float>& src,
                    double sampleRate);

    AudioEngine&        engine;
    juce::File          inputWav;
    juce::File          outputDir;
    juce::String        outputBaseName;
    int                 captureMask;
    int                 bitsPerSample;
    juce::String        resultMessage;
    bool                success { false };
};
