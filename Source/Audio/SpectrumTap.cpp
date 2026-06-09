#include "SpectrumTap.h"

SpectrumTap::SpectrumTap()
    : fft (kFftOrder)
{
    // Hann window pre-computed once.
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos ((float) (2.0 * juce::MathConstants<double>::pi * i)
                                                       / (float) (kFftSize - 1)));
}

void SpectrumTap::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    ring.fill (0.0f);
    writePos.store (0);
}

void SpectrumTap::pushStereo (const juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamps = buffer.getNumSamples();
    const int numCh    = buffer.getNumChannels();
    if (numSamps <= 0 || numCh <= 0) return;

    const float* l = buffer.getReadPointer (0);
    const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;

    int pos = writePos.load (std::memory_order_relaxed);
    for (int n = 0; n < numSamps; ++n)
    {
        ring[(size_t) pos] = 0.5f * (l[n] + r[n]);
        pos = (pos + 1) & (kRingSize - 1);
    }
    writePos.store (pos, std::memory_order_release);
}

bool SpectrumTap::produceMagnitudes (float* outDbBins,
                                     int    numBins,
                                     float  floorDb,
                                     float  ceilingDb) noexcept
{
    if (outDbBins == nullptr || numBins <= 0) return false;

    const int wp = writePos.load (std::memory_order_acquire);

    // Snapshot last kFftSize samples into fftBuf real component, applying window.
    int idx = (wp - kFftSize + kRingSize) & (kRingSize - 1);
    for (int i = 0; i < kFftSize; ++i)
    {
        fftBuf[(size_t) (2 * i)]     = ring[(size_t) idx] * window[(size_t) i];
        fftBuf[(size_t) (2 * i + 1)] = 0.0f;
        idx = (idx + 1) & (kRingSize - 1);
    }

    fft.performFrequencyOnlyForwardTransform (fftBuf.data());

    // After performFrequencyOnlyForwardTransform, fftBuf[0..kFftSize/2] holds
    // magnitudes (other half is mirror; ignore).
    const int   numUseful = kFftSize / 2;
    const float binHz     = (float) (sampleRate / (double) kFftSize);

    // Log-spaced bands 20 Hz .. min(20kHz, nyquist).
    const float fLo = 20.0f;
    const float fHi = juce::jmin (20000.0f, (float) (sampleRate * 0.5));
    const float logLo = std::log10 (fLo);
    const float logHi = std::log10 (fHi);

    // Reference: full-scale sine FFT magnitude in this FFT class is roughly
    // kFftSize/2 for a non-windowed input; windowed Hann reduces by ~6 dB.
    // We just normalise empirically so a 0 dBFS sine reads around 0 dB.
    const float norm = 2.0f / (float) kFftSize;

    for (int b = 0; b < numBins; ++b)
    {
        const float t0 = (float) b       / (float) numBins;
        const float t1 = (float) (b + 1) / (float) numBins;
        const float f0 = std::pow (10.0f, juce::jmap (t0, logLo, logHi));
        const float f1 = std::pow (10.0f, juce::jmap (t1, logLo, logHi));

        const int k0 = juce::jlimit (1, numUseful - 1, (int) (f0 / binHz));
        const int k1 = juce::jlimit (k0 + 1, numUseful, (int) (f1 / binHz) + 1);

        float peak = 0.0f;
        for (int k = k0; k < k1; ++k)
            peak = juce::jmax (peak, fftBuf[(size_t) k]);

        const float linear = peak * norm;
        const float db = juce::Decibels::gainToDecibels (linear, floorDb);
        outDbBins[b] = juce::jlimit (floorDb, ceilingDb, db);
    }
    return true;
}
