#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

/** Simple real-time pitch shifter using two crossfading delay taps.
 *
 *  Quality is fine for ±5 semitones (drop-tunings, octave-down riffing).
 *  Latency: ~grain/2 (≈ 30 ms with 60 ms grain). Mono in / mono out.
 *
 *  Bypassed when |semitones| < 0.05.
 */
class TransposeProcessor
{
public:
    void setSemitones (float st) noexcept { semitones.store (st); }
    float getSemitones() const noexcept   { return semitones.load(); }

    /** Latency contributed by this stage in samples. Zero when the shifter
     *  is effectively bypassed (|semitones| < 0.05) so the host doesn't
     *  flap PDC every time the knob touches zero; otherwise grain/2 (the
     *  worst-case Hann crossfade group delay). */
    int getLatencySamples() const noexcept
    {
        if (std::abs (semitones.load()) < 0.05f) return 0;
        return juce::roundToInt (grainSamples * 0.5f);
    }

    void prepare (double sr, int blockSize)
    {
        sampleRate = sr;
        juce::ignoreUnused (blockSize);
        const int sz = juce::nextPowerOfTwo ((int) (sr * 0.25)); // 250 ms ring
        buffer.assign ((size_t) sz, 0.0f);
        mask = sz - 1;
        writePos = 0;

        grainSamples = (float) (sr * 0.060); // 60 ms grain
        // Two read heads offset by half a grain in delay-time. The Hann
        // crossfade makes w(d) + w(d + g/2 mod g) == 1 so the sum is
        // continuous regardless of pitch direction.
        delayA = grainSamples * 0.25f;
        delayB = grainSamples * 0.75f;
    }

    void process (juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        if (n <= 0) return;

        const float st = semitones.load();
        if (std::abs (st) < 0.05f)
        {
            // Bypass — but keep buffer fresh so we don't pop on re-engage.
            if (buf.getNumChannels() > 0)
            {
                auto* x = buf.getReadPointer (0);
                for (int i = 0; i < n; ++i)
                {
                    buffer[(size_t) writePos] = x[i];
                    writePos = (writePos + 1) & mask;
                }
            }
            return;
        }

        // Playback ratio. ratio>1 = pitch UP, ratio<1 = pitch DOWN.
        const float ratio      = std::pow (2.0f, st / 12.0f);
        // The write head advances by 1 per output sample; the virtual read
        // head advances by 'ratio'. In delay-domain (delay = wp - read) the
        // per-sample change is therefore (1 - ratio):
        //   pitch up   (ratio>1)  -> delay shrinks  -> wraps from 0 to grain
        //   pitch down (ratio<1)  -> delay grows    -> wraps from grain to 0
        const float delayDelta = 1.0f - ratio;
        const float ringF      = (float) (mask + 1);
        const float g          = grainSamples;
        const float invG       = 1.0f / g;
        const float twoPi      = juce::MathConstants<float>::twoPi;

        // Only ever process the first channel — input is mono everywhere
        // upstream. Other channels are left untouched.
        if (buf.getNumChannels() <= 0) return;
        auto* x = buf.getWritePointer (0);

        for (int i = 0; i < n; ++i)
        {
            buffer[(size_t) writePos] = x[i];

            // Compute current read positions from delays.
            float pA = (float) writePos - delayA;
            float pB = (float) writePos - delayB;
            if (pA < 0.0f) pA += ringF;
            if (pB < 0.0f) pB += ringF;

            // Hann crossfade weights. Equal-power sum across the pair.
            const float wA = 0.5f - 0.5f * std::cos (twoPi * (delayA * invG));
            const float wB = 0.5f - 0.5f * std::cos (twoPi * (delayB * invG));

            x[i] = readFrac (pA) * wA + readFrac (pB) * wB;

            // Advance delays and wrap into (0, g). Wrapping by exactly one
            // grain length keeps the heads at the same relative offset to
            // each other so the Hann sum stays unity.
            delayA += delayDelta;
            delayB += delayDelta;
            if (delayA < 0.0f) delayA += g;
            if (delayA >= g)   delayA -= g;
            if (delayB < 0.0f) delayB += g;
            if (delayB >= g)   delayB -= g;

            writePos = (writePos + 1) & mask;
        }
    }

private:
    float readFrac (float pos) const
    {
        const int i0 = (int) pos;
        const float f = pos - (float) i0;
        const int i1 = (i0 + 1) & mask;
        return buffer[(size_t) (i0 & mask)] * (1.0f - f)
             + buffer[(size_t) i1] * f;
    }

    double sampleRate { 48000.0 };
    std::vector<float> buffer;
    int   mask     { 0 };
    int   writePos { 0 };
    float delayA   { 0.0f };
    float delayB   { 0.0f };
    float grainSamples { 2880.0f };

    std::atomic<float> semitones { 0.0f };
};
