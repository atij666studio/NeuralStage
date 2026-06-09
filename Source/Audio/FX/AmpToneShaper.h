#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

/** Three-macro tone shaper inserted before the amp model.
 *
 *  TIGHT (0..1, default 0)   — gentle high-pass that scrubs low rumble.
 *                              0 = bypass, 1 = HPF at ~180 Hz, Q ≈ 0.7.
 *  BODY  (0..1, default 0.5) — peaking bell at 700 Hz, ±5 dB.
 *  AIR   (0..1, default 0.5) — high shelf at 6 kHz, ±5 dB.
 *
 *  All bands are mono and re-coefficient at block boundaries, not per
 *  sample (CPU friendly; macros aren't expected to be modulated audio-rate).
 */
class AmpToneShaper
{
public:
    void prepare (double sr, int blockSize)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 1 };
        tight.prepare (spec);
        body .prepare (spec);
        air  .prepare (spec);
        updateCoeffs (true);
    }

    void reset()
    {
        tight.reset(); body.reset(); air.reset();
    }

    void setTight (float v) noexcept { tightMacro.store (juce::jlimit (0.0f, 1.0f, v)); }
    void setBody  (float v) noexcept { bodyMacro .store (juce::jlimit (0.0f, 1.0f, v)); }
    void setAir   (float v) noexcept { airMacro  .store (juce::jlimit (0.0f, 1.0f, v)); }

    void process (juce::AudioBuffer<float>& mono)
    {
        const int n = mono.getNumSamples();
        if (n <= 0 || mono.getNumChannels() <= 0) return;

        updateCoeffs (false);

        juce::dsp::AudioBlock<float> block (mono);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        const float t = tightMacro.load();
        const float b = bodyMacro .load();
        const float a = airMacro  .load();

        if (t > 0.001f) tight.process (ctx);
        if (std::abs (b - 0.5f) > 0.001f) body.process (ctx);
        if (std::abs (a - 0.5f) > 0.001f) air .process (ctx);
    }

private:
    void updateCoeffs (bool force)
    {
        const float t = tightMacro.load();
        const float b = bodyMacro .load();
        const float a = airMacro  .load();

        if (force || t != lastT)
        {
            // Tight = HPF, cutoff 20 Hz (no-op) -> 180 Hz, Q ≈ 0.707
            const float fc = juce::jmap (t, 0.0f, 1.0f, 20.0f, 180.0f);
            *tight.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, fc, 0.707f);
            lastT = t;
        }
        if (force || b != lastB)
        {
            // Body = peaking bell at 700 Hz, ±5 dB
            const float gainDb = juce::jmap (b, 0.0f, 1.0f, -5.0f, +5.0f);
            const float gain   = juce::Decibels::decibelsToGain (gainDb);
            *body.coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, 700.0f, 0.9f, gain);
            lastB = b;
        }
        if (force || a != lastA)
        {
            // Air = high shelf at 6 kHz, ±5 dB
            const float gainDb = juce::jmap (a, 0.0f, 1.0f, -5.0f, +5.0f);
            const float gain   = juce::Decibels::decibelsToGain (gainDb);
            *air.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 6000.0f, 0.707f, gain);
            lastA = a;
        }
    }

    double sampleRate { 48000.0 };
    juce::dsp::IIR::Filter<float> tight, body, air;
    std::atomic<float> tightMacro { 0.0f };
    std::atomic<float> bodyMacro  { 0.5f };
    std::atomic<float> airMacro   { 0.5f };
    float lastT { -1.0f }, lastB { -1.0f }, lastA { -1.0f };
};
