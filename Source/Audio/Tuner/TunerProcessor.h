#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <vector>

/** Lightweight chromatic tuner. Non-destructive: feed it the live input
 *  block and read `getDetectedFreq()` / `getCents()` from the UI thread.
 *
 *  Algorithm: YIN-style normalized difference function on a ~46 ms window
 *  (HPF'd to remove rumble). Updated about 12 times per second to keep
 *  CPU low; UI smooths the readout further.
 */
class TunerProcessor
{
public:
    void prepare (double sr, int blockSize)
    {
        sampleRate = sr;
        juce::ignoreUnused (blockSize);
        windowSize = juce::jmax (1024, (int) (sr * 0.046));   // ~46 ms
        ring.assign ((size_t) (windowSize * 2), 0.0f);
        writePos = 0;
        samplesUntilAnalyse = 0;
        diff.assign ((size_t) (windowSize / 2), 0.0f);

        // Pre-detector single-pole HPF state.
        hpfA = std::exp (-2.0f * juce::MathConstants<float>::pi * (float) (60.0 / sr));
        hpfX = hpfY = 0.0f;
    }

    void pushBlock (const juce::AudioBuffer<float>& mono)
    {
        const int n = mono.getNumSamples();
        if (n <= 0) return;
        const float* x = mono.getReadPointer (0);

        for (int i = 0; i < n; ++i)
        {
            // 1-pole HPF (remove DC / sub-rumble).
            const float xi = x[i];
            const float yi = hpfA * (hpfY + xi - hpfX);
            hpfX = xi;
            hpfY = yi;

            ring[(size_t) writePos] = yi;
            writePos = (writePos + 1) % (int) ring.size();
        }

        samplesUntilAnalyse -= n;
        if (samplesUntilAnalyse <= 0)
        {
            analyse();
            samplesUntilAnalyse = (int) (sampleRate / 12.0); // ~12 Hz
        }
    }

    float getFrequencyHz() const noexcept { return freqHz.load(); }
    float getConfidence()  const noexcept { return confidence.load(); }

    /** Returns nearest MIDI note + cents offset. note ∈ [0,127], cents ∈ [-50,+50]. */
    void getNoteAndCents (int& noteOut, float& centsOut) const noexcept
    {
        const float f = freqHz.load();
        if (f < 25.0f) { noteOut = -1; centsOut = 0.0f; return; }
        const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
        noteOut = (int) std::round (midi);
        centsOut = (midi - (float) noteOut) * 100.0f;
    }

    static juce::String noteName (int midi)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        if (midi < 0) return "--";
        return juce::String (names[(midi % 12 + 12) % 12]) + juce::String (midi / 12 - 1);
    }

private:
    void analyse()
    {
        const int W = windowSize;
        const int H = W / 2;

        // Copy newest W samples into a contiguous scratch.
        std::vector<float> x ((size_t) W);
        for (int i = 0; i < W; ++i)
        {
            int idx = writePos - W + i;
            const int sz = (int) ring.size();
            while (idx < 0) idx += sz;
            x[(size_t) i] = ring[(size_t) (idx % sz)];
        }

        // Quick energy gate to suppress noise/silence.
        double e = 0.0;
        for (int i = 0; i < W; ++i) e += x[(size_t) i] * x[(size_t) i];
        if (e / (double) W < 1.0e-5)
        {
            confidence.store (0.0f);
            return;
        }

        // YIN difference.
        for (int tau = 1; tau < H; ++tau)
        {
            float s = 0.0f;
            for (int i = 0; i < H; ++i)
            {
                const float d = x[(size_t) i] - x[(size_t) (i + tau)];
                s += d * d;
            }
            diff[(size_t) tau] = s;
        }
        diff[0] = 1.0f;

        // Cumulative mean normalized difference.
        double running = 0.0;
        for (int tau = 1; tau < H; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = (float) (diff[(size_t) tau] * tau / running);
        }

        // Find first dip below threshold.
        const float threshold = 0.15f;
        int tauEst = -1;
        for (int tau = 2; tau < H - 1; ++tau)
        {
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 < H && diff[(size_t) (tau + 1)] < diff[(size_t) tau]) ++tau;
                tauEst = tau;
                break;
            }
        }

        if (tauEst < 0)
        {
            confidence.store (0.0f);
            return;
        }

        // Parabolic interpolation around tauEst for sub-sample accuracy.
        const float s0 = diff[(size_t) (tauEst - 1)];
        const float s1 = diff[(size_t)  tauEst];
        const float s2 = diff[(size_t) (tauEst + 1)];
        const float denom = (s0 + s2 - 2.0f * s1);
        const float shift = (denom != 0.0f) ? 0.5f * (s0 - s2) / denom : 0.0f;
        const float tauRefined = (float) tauEst + shift;

        const float f = (float) sampleRate / tauRefined;
        if (f >= 30.0f && f <= 1500.0f)
        {
            freqHz.store (f);
            confidence.store (1.0f - juce::jlimit (0.0f, 1.0f, s1));
        }
    }

    double sampleRate { 48000.0 };
    int    windowSize { 0 };
    std::vector<float> ring, diff;
    int    writePos { 0 };
    int    samplesUntilAnalyse { 0 };

    float hpfA { 0.0f }, hpfX { 0.0f }, hpfY { 0.0f };

    std::atomic<float> freqHz     { 0.0f };
    std::atomic<float> confidence { 0.0f };
};
