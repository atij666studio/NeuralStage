#pragma once

#if NS_HAVE_NAM_CORE

#include <memory>
#include <functional>
#include <cmath>
#include <stdexcept>

#include "NAM/dsp.h"

// -----------------------------------------------------------------------------
// AudioDSPTools / iPlug2 compatibility shim.
//
// ResamplingContainer.h and LanczosResampler.h were lifted from iPlug2 and
// still reference two iPlug2 symbols (`DEFAULT_BLOCK_SIZE` and `iplug::PI`)
// that do not exist in this standalone JUCE build. They also use std::min /
// std::max, which collide with the function-like min()/max() macros that
// <windows.h> drags in. Provide the symbols and neutralise the macros BEFORE
// including the resampler headers.
// -----------------------------------------------------------------------------
#ifdef max
 #undef max
#endif
#ifdef min
 #undef min
#endif

#ifndef DEFAULT_BLOCK_SIZE
 #define DEFAULT_BLOCK_SIZE 1024
#endif

namespace iplug
{
    // iPlug2 defines this as a double constexpr; match that exactly.
    static constexpr double PI = 3.1415926535897932384626433832795;
}

#include "dsp/ResamplingContainer/ResamplingContainer.h"

// =============================================================================
// ResamplingNAM
//
// Faithful port of Steve Atkinson's `ResamplingNAM` wrapper from the official
// NeuralAmpModelerPlugin (NeuralAmpModeler/ResamplingNAM.h). It runs the
// encapsulated NAM model at its NATIVE training sample rate, resampling the
// session audio to and from that rate with a Lanczos resampler.
//
// This is what gives the reference plugin its exact tone: a NAM model is a
// neural network whose per-sample time-steps are tied to the sample rate it
// was trained at. Running it at any OTHER rate (for example via fixed 2x/4x
// oversampling) shifts its entire frequency response, transient feel, clarity
// and low-end. Resampling to the model's native rate -- exactly as the
// reference does -- reproduces the captured amp's response sample-for-sample.
//
// When the session sample rate already equals the model's native rate (the
// common 48 kHz case) NeedToResample() is false and the model runs directly
// with zero added latency, identical to the reference.
// =============================================================================

/** Resolve a model's native sample rate. Older NAM captures predate the
 *  sample-rate metadata field; like the reference, we assume 48 kHz for those
 *  (true for the overwhelming majority of historical models). */
inline double GetNAMSampleRate (const std::unique_ptr<nam::DSP>& model)
{
    constexpr double assumedSampleRate = 48000.0;
    const double reported = model->GetExpectedSampleRate();
    return reported <= 0.0 ? assumedSampleRate : reported;
}

class ResamplingNAM : public nam::DSP
{
public:
    ResamplingNAM (std::unique_ptr<nam::DSP> encapsulated, const double expected_sample_rate)
        : nam::DSP (encapsulated->NumInputChannels(),
                    encapsulated->NumOutputChannels(),
                    expected_sample_rate),
          mEncapsulated (std::move (encapsulated)),
          mResampler (GetNAMSampleRate (mEncapsulated))
    {
        // Bind the encapsulated model's process() so the resampler can call it
        // at the model's native rate.
        mBlockProcessFunc = [this] (NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames)
        {
            mEncapsulated->process (input, output, numFrames);
        };

        // Mirror the encapsulated model's level metadata so the host sees the
        // same loudness / input / output levels it would from the bare model.
        if (mEncapsulated->HasLoudness())    SetLoudness    (mEncapsulated->GetLoudness());
        if (mEncapsulated->HasInputLevel())  SetInputLevel  (mEncapsulated->GetInputLevel());
        if (mEncapsulated->HasOutputLevel()) SetOutputLevel (mEncapsulated->GetOutputLevel());

        constexpr int maxBlockSize = 2048; // conservative; host re-Resets us
        Reset (expected_sample_rate, maxBlockSize);
    }

    ~ResamplingNAM() override = default;

    void prewarm() override { mEncapsulated->prewarm(); }

    void process (NAM_SAMPLE** input, NAM_SAMPLE** output, const int num_frames) override
    {
        if (num_frames > mMaxExternalBlockSize)
            throw std::runtime_error ("More frames were provided than the max expected!");

        if (! NeedToResample())
            mEncapsulated->process (input, output, num_frames);
        else
            mResampler.ProcessBlock (input, output, num_frames, mBlockProcessFunc);
    }

    /** Host-domain latency added by the resampler (0 when running natively). */
    int GetLatency() const { return NeedToResample() ? mResampler.GetLatency() : 0; }

    void Reset (const double sampleRate, const int maxBlockSize) override
    {
        mExpectedSampleRate   = sampleRate;
        mMaxExternalBlockSize = maxBlockSize;
        mResampler.Reset (sampleRate, maxBlockSize);

        // Size the encapsulated model's buffers for the resampled block it
        // will actually receive (matches the reference plugin's HACK comment).
        // Pass the MODEL'S native sample rate (not the session rate) so the
        // encapsulated model is always reset at the rate it was trained at.
        const double modelSr   = GetEncapsulatedSampleRate();
        const double upRatio   = sampleRate / modelSr;
        const auto maxEncapsulatedBlockSize =
            static_cast<int> (std::ceil (static_cast<double> (maxBlockSize) / upRatio));
        mEncapsulated->ResetAndPrewarm (modelSr, maxEncapsulatedBlockSize);
    }

    double GetEncapsulatedSampleRate() const { return GetNAMSampleRate (mEncapsulated); }

    /** Raw pointer to the wrapped model (e.g. to reach the nam::SlimmableModel
     *  interface for slim-size control). Ownership stays with this object. */
    nam::DSP* GetEncapsulated() const noexcept { return mEncapsulated.get(); }

private:
    bool NeedToResample() const { return GetExpectedSampleRate() != GetEncapsulatedSampleRate(); }

    std::unique_ptr<nam::DSP> mEncapsulated;
    dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> mResampler;
    int mMaxExternalBlockSize = 0;
    std::function<void (NAM_SAMPLE**, NAM_SAMPLE**, int)> mBlockProcessFunc;
};

#endif // NS_HAVE_NAM_CORE
