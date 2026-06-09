#include "InputProcessor.h"

void InputProcessor::prepare (double sampleRate, int /*blockSize*/)
{
    smoothedGain.reset (sampleRate, 0.030); // 30 ms ramp -- click-free mute / knob moves
    smoothedGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (preGainDb.load()));

    // DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1].
    // R = 1 - 2*pi*fc/Fs, choose fc ~= 20 Hz (below low-E fundamental 82 Hz,
    // above any DC / sub-bass rumble). Per-sample-rate so cutoff stays fixed.
    const double fc = 20.0;
    dcR = (float) juce::jlimit (0.0, 0.99999,
                                1.0 - juce::MathConstants<double>::twoPi * fc / juce::jmax (1.0, sampleRate));
    for (int c = 0; c < kMaxInCh; ++c) { dcX1[c] = 0.0f; dcY1[c] = 0.0f; }
}

void InputProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const float target = muted.load() ? 0.0f
                                      : juce::Decibels::decibelsToGain (preGainDb.load());
    smoothedGain.setTargetValue (target);

    const int numCh = buffer.getNumChannels();
    const int numS  = buffer.getNumSamples();

    if (smoothedGain.isSmoothing())
    {
        constexpr int kMaxN = 4096;
        float gains[kMaxN];

        int offset = 0;
        while (offset < numS)
        {
            const int n = juce::jmin (kMaxN, numS - offset);
            for (int i = 0; i < n; ++i)
                gains[i] = smoothedGain.getNextValue();

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = buffer.getWritePointer (ch) + offset;
                for (int i = 0; i < n; ++i)
                    d[i] *= gains[i];
            }
            offset += n;
        }
    }
    else
    {
        const float g = smoothedGain.getCurrentValue();
        if (g == 0.0f)
        {
            buffer.clear();
            measured.store (0.0f);
            return;
        }
        if (g != 1.0f) buffer.applyGain (g);
    }

    // Cheap peak measurement for calibration UI (post-gain).
    float peak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numS));
    measured.store (peak);

    // DC blocker AFTER gain so the HPF cleans residual DC introduced by any
    // gain-stage asymmetry too. Per-channel state, in-place.
    const int chDc = juce::jmin (numCh, kMaxInCh);
    for (int c = 0; c < chDc; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        float x1 = dcX1[c], y1 = dcY1[c];
        for (int i = 0; i < numS; ++i)
        {
            const float x = d[i];
            const float y = x - x1 + dcR * y1;
            d[i] = y;
            x1 = x; y1 = y;
        }
        dcX1[c] = x1; dcY1[c] = y1;
    }
}
