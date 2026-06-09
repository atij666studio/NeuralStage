#include "NoiseGate.h"

void NoiseGate::prepare (double sampleRate, int /*blockSize*/)
{
    currentSR  = sampleRate > 0.0 ? sampleRate : 48000.0;
    envelope   = 0.0f;
    holdCounter = 0;
    coefsDirty.store (true);
    updateCoefficients (currentSR);
}

void NoiseGate::updateCoefficients (double sr) noexcept
{
    // One-pole exponential: coef = exp (-1 / (timeSec * sr))
    // Larger coef = slower envelope movement.
    const float aSec = juce::jmax (0.0001f, attackMs.load()  * 0.001f);
    const float rSec = juce::jmax (0.0001f, releaseMs.load() * 0.001f);
    attackCoef  = std::exp (-1.0f / (aSec * (float) sr));
    releaseCoef = std::exp (-1.0f / (rSec * (float) sr));
    holdSamples = (int) (holdMs.load() * 0.001f * (float) sr);
    coefsDirty.store (false);
}

void NoiseGate::process (juce::AudioBuffer<float>& buffer)
{
    if (! enabled.load())
        return;

    if (coefsDirty.load())
        updateCoefficients (currentSR);

    const float thresh   = juce::Decibels::decibelsToGain (thresholdDb.load());
    const int   numCh    = buffer.getNumChannels();
    const int   numSamps = buffer.getNumSamples();

    for (int n = 0; n < numSamps; ++n)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getReadPointer (ch)[n]));

        // Hold logic: if input crosses threshold, force gate fully open and
        // reset hold counter. While holdCounter > 0, target stays at 1.
        float target;
        if (peak > thresh)
        {
            target      = 1.0f;
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            target = 1.0f;
            --holdCounter;
        }
        else
        {
            target = 0.0f;
        }

        // Asymmetric one-pole smoothing toward target.
        const float coef = target > envelope ? attackCoef : releaseCoef;
        envelope = target + (envelope - target) * coef;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[n] *= envelope;
    }
}
