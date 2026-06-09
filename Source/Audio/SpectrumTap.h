#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

/** Lock-free spectrum analyser tap.
 *
 *  Audio thread: calls pushStereo() once per process block. Samples are
 *  summed to mono and written into a 4 x fftSize ring; no allocation, no
 *  locks. Latest-N-samples wins -- if the UI thread can't drain fast enough
 *  we simply overwrite the oldest block. That's fine for a visualiser.
 *
 *  UI thread: calls produceMagnitudes() at e.g. 30 Hz. It snapshots the
 *  most-recent fftSize samples, applies a Hann window, runs a forward FFT,
 *  and writes magnitude-in-dB values into the caller's output array using
 *  log-spaced frequency bands from 20 Hz to 20 kHz.
 *
 *  Designed to be embedded in AudioEngine and queried by SpectrumOverlay.
 */
class SpectrumTap
{
public:
    static constexpr int kFftOrder = 11;            // 2^11 = 2048 samples
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kRingSize = kFftSize * 4;  // headroom against UI jitter

    SpectrumTap();

    void prepare (double sampleRate);

    /** Audio-thread: push a block (mono or stereo). */
    void pushStereo (const juce::AudioBuffer<float>& buffer) noexcept;

    /** UI-thread: fill `outDbBins` with `numBins` log-spaced magnitudes
     *  (in dBFS, clamped to [floorDb, ceilingDb]). Returns true if a
     *  fresh FFT was performed; false if not enough new samples have
     *  arrived since the last call.
     */
    bool produceMagnitudes (float* outDbBins,
                            int    numBins,
                            float  floorDb   = -80.0f,
                            float  ceilingDb =   0.0f) noexcept;

    double getSampleRate() const noexcept { return sampleRate; }

private:
    juce::dsp::FFT                       fft;
    std::array<float, kFftSize>          window;
    std::array<float, kFftSize * 2>      fftBuf {};

    // Audio-thread writes here; UI-thread reads.
    std::array<float, kRingSize>         ring {};
    std::atomic<int>                     writePos { 0 };

    double sampleRate { 48000.0 };
};
