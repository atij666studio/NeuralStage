#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>

namespace na
{
    // ------------------------------------------------------------------
    // NAM Level — auto output leveler (END OF CHAIN).
    //
    // Concept: keep perceived loudness consistent regardless of what
    // happens upstream (riff change, palm mute, big chord, lead boost…).
    //
    // Single LEVEL macro (0..1):
    //   Soft   (0.0) : slow, gentle. Long time constants, small max
    //                  correction range. Preserves dynamics.
    //   Locked (1.0) : fast, strict. Short time constants, wide
    //                  correction range. Keeps level rock-steady.
    //
    // Auto toggle: when OFF the gain stage is bypassed (passthrough)
    // but the meter still shows output level — useful for A/B.
    //
    // Algorithm:
    //   1. Track short-term RMS of the input (window scaled by macro).
    //   2. Compare to a fixed internal target (-18 dBFS RMS).
    //   3. Compute correction gain in dB, clamp to allowed range,
    //      smooth it with attack/release time constants (also macro
    //      dependent — asymmetric, attack always faster than release
    //      so we duck loud parts quickly but lift quiet parts gently).
    //   4. Apply gain. Final soft-clipper / brickwall to catch any
    //      remaining peaks above 0 dBFS.
    //
    // The macro shapes time constants and max correction together so
    // the knob always feels coherent: more LEVEL = tighter leash.
    // ------------------------------------------------------------------
    class LevelDSP
    {
    public:
        // ---- Public meters (read by editor on a UI timer) ---------------
        std::atomic<float> inputDb     { -90.0f };
        std::atomic<float> outputDb    { -90.0f };
        std::atomic<float> gainDb      {   0.0f };  // current applied correction
        std::atomic<float> grAmount    {   0.0f };  // 0..1 — abs gain change vs max

        void prepare (double sr, int blockSize, int numChannels)
        {
            sampleRate = sr;
            channels   = juce::jmax (1, numChannels);
            juce::ignoreUnused (blockSize);

            // RMS window — base 300 ms; modulated by macro at runtime.
            // Pre-allocate worst-case (longest window = ~2 s).
            const int maxWin = (int) std::ceil (sr * 2.0);
            sqBuf.assign ((size_t) juce::jmax (1, maxWin) + 4, 0.0f);
            sqWritePos = 0;
            sqSum      = 0.0;
            curWinSize = juce::jmax (1, (int) std::round (sr * 0.3));

            envIn         = 0.0f;
            envOut        = 0.0f;
            smoothedGain  = 1.0f;
            lastMacro     = -1.0f;
            lastAuto      = -1;
        }

        void process (juce::AudioBuffer<float>& buffer,
                      float macro, bool autoOn, float trimDb)
        {
            const int numCh = juce::jmin (channels, buffer.getNumChannels());
            const int n     = buffer.getNumSamples();
            if (numCh <= 0 || n <= 0) return;

            const float macroClamped = juce::jlimit (0.0f, 1.0f, macro);

            // ---- Macro mapping ------------------------------------------
            // Window length: 600 ms (Soft) -> 120 ms (Locked).
            // Smaller window = faster RMS response = more aggressive.
            const double winSec   = 0.600 - 0.480 * (double) macroClamped;
            const int    winSize  = juce::jlimit (32,
                                                  (int) sqBuf.size() - 4,
                                                  (int) std::round (sampleRate * winSec));

            // Resize window if it changed (also resets running sum to
            // avoid drift / negative values from float error).
            if (winSize != curWinSize)
            {
                curWinSize = winSize;
                sqSum      = 0.0;
                std::fill (sqBuf.begin(), sqBuf.end(), 0.0f);
                sqWritePos = 0;
            }

            // Attack/release time constants. Attack faster than release.
            // Soft   : att 250 ms, rel 1500 ms
            // Locked : att  20 ms, rel  150 ms
            const double attMs = 250.0 - 230.0 * (double) macroClamped;
            const double relMs = 1500.0 - 1350.0 * (double) macroClamped;
            const float  attA  = (float) (1.0 - std::exp (-1.0 / (0.001 * attMs * sampleRate)));
            const float  relA  = (float) (1.0 - std::exp (-1.0 / (0.001 * relMs * sampleRate)));

            // Max correction range (dB).
            // Soft   :  ±4 dB
            // Locked : ±18 dB
            const float maxCutDb   = 4.0f + 14.0f * macroClamped;  // how much we can lower
            const float maxBoostDb = 4.0f + 14.0f * macroClamped;  // how much we can lift

            // Internal loudness target: -18 dBFS RMS. Output trim is
            // applied AFTER correction so the user can offset the final
            // output level without changing the leveling action.
            const float targetRms   = juce::Decibels::decibelsToGain (-18.0f);
            const float trimGain    = juce::Decibels::decibelsToGain (trimDb);
            const float envSmooth   = (float) (1.0 - std::exp (-1.0 / (0.020 * sampleRate)));

            const int chClamp = juce::jmin (numCh, 8);
            float* writePtrs[8] = { nullptr };
            for (int c = 0; c < chClamp; ++c)
                writePtrs[c] = buffer.getWritePointer (c);

            const int   winSz  = curWinSize;
            const int   bufSz  = (int) sqBuf.size();
            const float invWin = 1.0f / (float) winSz;

            float pkIn = 0.0f, pkOut = 0.0f;
            float lastAppliedDb = juce::Decibels::gainToDecibels (smoothedGain, -60.0f);

            for (int i = 0; i < n; ++i)
            {
                // --- Mono-sum sample for RMS measurement ----------------
                float mono = 0.0f;
                for (int c = 0; c < chClamp; ++c) mono += writePtrs[c][i];
                if (chClamp > 0) mono /= (float) chClamp;

                const float sq = mono * mono;

                // Sliding-window RMS via running sum of squares.
                int rp = sqWritePos - winSz;
                if (rp < 0) rp += bufSz;
                const float oldSq = sqBuf[(size_t) rp];
                sqSum += (double) sq - (double) oldSq;
                sqBuf[(size_t) sqWritePos] = sq;
                if (++sqWritePos >= bufSz) sqWritePos = 0;

                // Guard against fp drift.
                if (sqSum < 0.0) sqSum = 0.0;

                const float meanSq = (float) (sqSum * (double) invWin);
                const float rms    = std::sqrt (juce::jmax (1.0e-12f, meanSq));

                // --- Desired correction (dB) ----------------------------
                float corrDb = juce::Decibels::gainToDecibels (targetRms / rms, -60.0f);
                corrDb = juce::jlimit (-maxCutDb, maxBoostDb, corrDb);

                // Below noise floor → don't try to lift silence.
                if (rms < 1.0e-4f) corrDb = 0.0f;

                const float targetGain = juce::Decibels::decibelsToGain (corrDb);

                // Asymmetric smoothing: attack when we need to REDUCE gain
                // (signal got loud), release when we need to ADD gain.
                const float a = (targetGain < smoothedGain) ? attA : relA;
                smoothedGain += (targetGain - smoothedGain) * a;

                // --- Apply ----------------------------------------------
                const float applyGain = autoOn ? smoothedGain * trimGain
                                               : trimGain;

                for (int c = 0; c < chClamp; ++c)
                {
                    float y = writePtrs[c][i] * applyGain;

                    // Soft clipper for safety — only kicks in above ~-1 dB.
                    if (y >  0.99f) y = 0.99f + std::tanh ((y - 0.99f) * 4.0f) * 0.01f;
                    if (y < -0.99f) y = -0.99f + std::tanh ((y + 0.99f) * 4.0f) * 0.01f;

                    writePtrs[c][i] = y;

                    const float aOut = std::abs (y);
                    if (aOut > pkOut) pkOut = aOut;
                }

                const float aIn = std::abs (mono);
                if (aIn > pkIn) pkIn = aIn;

                lastAppliedDb = juce::Decibels::gainToDecibels (
                                    juce::jmax (1.0e-6f, smoothedGain), -60.0f);
            }

            envIn  += (pkIn  - envIn)  * envSmooth;
            envOut += (pkOut - envOut) * envSmooth;

            const float maxRange = juce::jmax (maxCutDb, maxBoostDb);
            const float grNorm   = juce::jlimit (0.0f, 1.0f,
                                                 std::abs (lastAppliedDb) / juce::jmax (0.1f, maxRange));

            inputDb.store  (juce::Decibels::gainToDecibels (envIn,  -90.0f), std::memory_order_relaxed);
            outputDb.store (juce::Decibels::gainToDecibels (envOut, -90.0f), std::memory_order_relaxed);
            gainDb.store   (autoOn ? lastAppliedDb : 0.0f,                   std::memory_order_relaxed);
            grAmount.store (autoOn ? grNorm        : 0.0f,                   std::memory_order_relaxed);

            lastMacro = macroClamped;
            lastAuto  = autoOn ? 1 : 0;
        }

    private:
        double sampleRate { 48000.0 };
        int    channels   { 2 };

        std::vector<float> sqBuf;
        int    sqWritePos { 0 };
        double sqSum      { 0.0 };
        int    curWinSize { 0 };

        float envIn        { 0.0f };
        float envOut       { 0.0f };
        float smoothedGain { 1.0f };
        float lastMacro    { -1.0f };
        int   lastAuto     { -1 };
    };
}
