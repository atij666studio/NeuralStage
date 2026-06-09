#include "OutputProcessor.h"

void OutputProcessor::prepare (double sampleRate, int /*blockSize*/)
{
    smoothedGain.reset (sampleRate, 0.030); // 30 ms ramp -- kills mute / knob-zip clicks
    smoothedGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (postGainDb.load()));

    // Scene-switch click-guard duck timing (samples/step from this SR).
    duckOutStep = (float) (1.0 / juce::jmax (1.0, 0.003 * sampleRate)); // 3 ms fade out
    duckInStep  = (float) (1.0 / juce::jmax (1.0, 0.028 * sampleRate)); // 28 ms fade in
    duckHoldLen = juce::jmax (1, (int) (0.010 * sampleRate));           // 10 ms hold
    duckPhase   = 0;
    duckGain    = 1.0f;
    duckHoldRem = 0;
    duckSeenGen = sceneSwitchGen.load();

    // Safety-limiter ballistics: 1 ms attack (fast clamp), 50 ms release.
    const double attackSec  = 0.001;
    const double releaseSec = 0.050;
    attackCoef  = (float) std::exp (-1.0 / (attackSec  * sampleRate));
    releaseCoef = (float) std::exp (-1.0 / (releaseSec * sampleRate));
    envelope = 1.0f;
    lastReductionDb.store (0.0f);

    // Peak meter: ~300 ms decay to silence (per-sample multiplicative).
    peakDecay = (float) std::exp (-1.0 / (0.300 * sampleRate));
    peakHold  = 0.0f;
    peakDb.store (-60.0f);
    rmsDb .store (-60.0f);

    loudnessSr   = sampleRate;
    loudnessMs   = 0.0f;
    loudnessDb.store (-70.0f);

    // Integrated-loudness state (100 ms hop).
    intHopSamples   = juce::jmax (1, (int) std::round (0.100 * sampleRate));
    intHopRemaining = intHopSamples;
    intHopAccumMs   = 0.0f;
    intHopSampleN   = 0;
    for (auto& v : intRingMs) v = 0.0f;
    intRingFilled = 0;
    intRingNext   = 0;
    intBlocks.clear();
    intBlocks.reserve (1024);
    integratedDb.store (-70.0f);
    resetIntegrated.store (false);

    // True-peak meter: 4x polyphase IIR oversampler. Lazy-init in process()
    // when the actual channel count + block size is known (avoids vDSP setup
    // storms during repeated prepare() in hosts).
    truePeakOs.reset();
    truePeakOsCh  = 0;
    truePeakOsBlk = 0;
    truePeakDb.store (-60.0f);
    truePeakHold = 0.0f;

    // ITU-R BS.1770 K-weighting filters (RBJ biquad design at this SR).
    auto designHighShelf = [sampleRate] (Biquad& bq, double fc, double gainDb, double Q)
    {
        const double A    = std::pow (10.0, gainDb / 40.0);
        const double w0   = juce::MathConstants<double>::twoPi * fc / sampleRate;
        const double cw   = std::cos (w0);
        const double sw   = std::sin (w0);
        const double alpha= sw / (2.0 * Q);
        const double sa2  = 2.0 * std::sqrt (A) * alpha;
        const double a0   =       (A + 1.0) - (A - 1.0) * cw + sa2;
        bq.b0 = (float) ( A * ((A + 1.0) + (A - 1.0) * cw + sa2) / a0);
        bq.b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * cw)   / a0);
        bq.b2 = (float) ( A * ((A + 1.0) + (A - 1.0) * cw - sa2) / a0);
        bq.a1 = (float) (   2.0 * ((A - 1.0) - (A + 1.0) * cw)   / a0);
        bq.a2 = (float) (       ((A + 1.0) - (A - 1.0) * cw - sa2) / a0);
        bq.x1 = bq.x2 = bq.y1 = bq.y2 = 0.0f;
    };
    auto designHighPass = [sampleRate] (Biquad& bq, double fc, double Q)
    {
        const double w0   = juce::MathConstants<double>::twoPi * fc / sampleRate;
        const double cw   = std::cos (w0);
        const double sw   = std::sin (w0);
        const double alpha= sw / (2.0 * Q);
        const double a0   = 1.0 + alpha;
        bq.b0 = (float) ( (1.0 + cw) * 0.5 / a0);
        bq.b1 = (float) (-(1.0 + cw)       / a0);
        bq.b2 = (float) ( (1.0 + cw) * 0.5 / a0);
        bq.a1 = (float) (-2.0 * cw         / a0);
        bq.a2 = (float) ( (1.0 - alpha)    / a0);
        bq.x1 = bq.x2 = bq.y1 = bq.y2 = 0.0f;
    };
    for (int ch = 0; ch < kMaxLoudnessCh; ++ch)
    {
        designHighShelf (kPre[ch], 1681.974450955533, 3.999843853973347, 0.7071752369554196);
        designHighPass  (kRlb[ch],   38.13547087602444,                    0.5003270373702559);
    }

    // Slow peak-hold marker: hold flat for 1.5 s, then drop ~12 dB/s.
    slowHoldReset  = (int) (1.5 * sampleRate);
    slowHoldFrames = 0;
    slowHold       = 0.0f;
    slowHoldFall   = (float) std::pow (10.0, -12.0 / 20.0 / sampleRate);
    peakHoldDb.store (-60.0f);
    clipLatched.store (false);
}

void OutputProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const float target = muted.load() ? 0.0f
                                      : juce::Decibels::decibelsToGain (postGainDb.load() + sceneTrimDb.load() + abTrimDb.load());
    smoothedGain.setTargetValue (target);

    const int numCh = buffer.getNumChannels();
    const int numS  = buffer.getNumSamples();

    // -------- 0. Scene-switch click guard (fade out -> hold -> fade in). --------
    // Masks chain / NAM-model swap discontinuities so a scene change is silent
    // and near-instant. Triggered by notifySceneSwitch() from SceneManager.
    {
        const int g = sceneSwitchGen.load();
        if (g != duckSeenGen)
        {
            duckSeenGen = g;
            duckPhase   = 1;            // begin fade out
            duckHoldRem = duckHoldLen;
        }
        if (duckPhase != 0 && numCh > 0 && numS > 0)
        {
            for (int s = 0; s < numS; ++s)
            {
                switch (duckPhase)
                {
                    case 1: duckGain -= duckOutStep;
                            if (duckGain <= 0.0f) { duckGain = 0.0f; duckPhase = 2; }
                            break;
                    case 2: if (--duckHoldRem <= 0) duckPhase = 3;
                            break;
                    case 3: duckGain += duckInStep;
                            if (duckGain >= 1.0f) { duckGain = 1.0f; duckPhase = 0; }
                            break;
                    default: break;
                }
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.getWritePointer (ch)[s] *= duckGain;
            }
        }
    }

    // -------- 1. Master gain (smoothed). --------
    if (! smoothedGain.isSmoothing())
    {
        const float g = smoothedGain.getCurrentValue();
        if (g == 0.0f)
        {
            buffer.clear();
            lastReductionDb.store (0.0f);
            // Let the meter decay to silence rather than freezing.
            peakHold *= std::pow (peakDecay, (float) juce::jmax (1, numS));
            peakDb.store (juce::Decibels::gainToDecibels (peakHold, -60.0f));
            rmsDb .store (-60.0f);
            return;
        }
        if (g != 1.0f) buffer.applyGain (g);
    }
    else
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

    // -------- 2. Safety limiter (true brickwall, only catches real clips). --------
    // This is a STANDALONE safety net to protect speakers from runaway peaks,
    // NOT a mastering limiter.  It must be transparent on normal signal --
    // i.e. it should do absolutely nothing until the output would otherwise
    // exceed the ceiling.  A 1 dB knee just below the ceiling guarantees that
    // the dynamic range of a hot NAM model is preserved (no constant gain
    // reduction on every transient as before, which was being heard as
    // "squashed / harsh / no clarity").
    if (limiterOn.load() && numCh > 0 && numS > 0)
    {
        const float ceiling = juce::Decibels::decibelsToGain (ceilingDb.load());
        const float thrDb   = ceilingDb.load();
        constexpr float kneeDb = 1.0f;       // narrow knee -> true safety brickwall
        constexpr float halfKneeDb = kneeDb * 0.5f;
        const float kneeStartGain = juce::Decibels::decibelsToGain (thrDb - halfKneeDb);

        float minEnv = 1.0f;

        for (int s = 0; s < numS; ++s)
        {
            // Linked peak across channels.
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::fabs (buffer.getReadPointer (ch)[s]));

            // Soft-knee target gain reduction (Reiss/Zolzer formulation).
            // Below knee:                t = 1.0
            // Inside knee (T-K/2 .. T+K/2): quadratic blend
            // Above knee:                t = ceiling / peak  (hard limit)
            float t = 1.0f;
            if (peak > kneeStartGain && peak > 0.0f)
            {
                const float pDb    = juce::Decibels::gainToDecibels (peak, -120.0f);
                const float diffDb = pDb - thrDb;            // distance above (or below) threshold
                float reductionDb;
                if (2.0f * diffDb >= kneeDb)
                    reductionDb = diffDb;                    // hard above knee
                else
                    reductionDb = (diffDb + halfKneeDb) * (diffDb + halfKneeDb)
                                / (2.0f * kneeDb);           // quadratic in-knee
                t = juce::Decibels::decibelsToGain (-reductionDb);
            }

            const float coef = (t < envelope) ? attackCoef : releaseCoef;
            envelope = t + coef * (envelope - t);

            // Final hard ceiling guard (defensive against floating-point drift).
            float applied = envelope;
            if (peak * applied > ceiling && peak > 0.0f)
                applied = ceiling / peak;

            if (applied < 1.0f)
            {
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.getWritePointer (ch)[s] *= applied;
            }

            if (applied < minEnv) minEnv = applied;
        }

        lastReductionDb.store (minEnv >= 1.0f ? 0.0f
                                              : juce::Decibels::gainToDecibels (minEnv, -60.0f));
    }
    else
    {
        if (envelope < 1.0f) envelope = 1.0f;
        lastReductionDb.store (0.0f);
    }

    // -------- 3. Output level meters (post-everything). --------
    {
        double sumSq = 0.0;
        long   count = 0;
        float  blockPeak = 0.0f;
        bool   clipped   = false;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* d = buffer.getReadPointer (ch);
            for (int s = 0; s < numS; ++s)
            {
                const float a = std::fabs (d[s]);
                if (a > peakHold) peakHold = a;
                if (a > blockPeak) blockPeak = a;
                if (a >= 0.999f) clipped = true;
                sumSq += (double) d[s] * (double) d[s];
            }
            count += numS;
        }
        // Decay the peak so the meter actually falls between transients.
        peakHold *= std::pow (peakDecay, (float) numS);

        const float rms = (count > 0)
                            ? (float) std::sqrt (sumSq / (double) count)
                            : 0.0f;
        peakDb.store (juce::Decibels::gainToDecibels (peakHold, -60.0f));
        rmsDb .store (juce::Decibels::gainToDecibels (rms,      -60.0f));

        // ITU-R BS.1770 short-term loudness (LUFS): K-weight each channel,
        // sum mean-square across channels (L=R=1), EMA over ~3 s, then
        // L_K = -0.691 + 10*log10(sum). Clamped to -70 LUFS minimum.
        double kSumSq = 0.0;
        const int kCh = juce::jmin (numCh, (int) kMaxLoudnessCh);
        for (int ch = 0; ch < kCh; ++ch)
        {
            auto& bp = kPre[ch];
            auto& br = kRlb[ch];
            const auto* d = buffer.getReadPointer (ch);
            for (int s = 0; s < numS; ++s)
            {
                const float x  = d[s];
                const float y1 = bp.b0*x + bp.b1*bp.x1 + bp.b2*bp.x2 - bp.a1*bp.y1 - bp.a2*bp.y2;
                bp.x2 = bp.x1; bp.x1 = x; bp.y2 = bp.y1; bp.y1 = y1;
                const float y2 = br.b0*y1 + br.b1*br.x1 + br.b2*br.x2 - br.a1*br.y1 - br.a2*br.y2;
                br.x2 = br.x1; br.x1 = y1; br.y2 = br.y1; br.y1 = y2;
                kSumSq += (double) y2 * (double) y2;
            }
        }
        const float blockMs = (numS > 0)
                                ? (float) (kSumSq / (double) numS)  // L+R sum-of-MS, denominator is numS (per-channel time average; channels added)
                                : 0.0f;
        const float alpha = (float) std::exp (-(double) numS / (loudnessTau * loudnessSr));
        loudnessMs = alpha * loudnessMs + (1.0f - alpha) * blockMs;
        const float lufs = (loudnessMs > 1.0e-12f)
                            ? -0.691f + 10.0f * std::log10 (loudnessMs)
                            : -70.0f;
        loudnessDb.store (juce::jmax (-70.0f, lufs));

        // ---- Integrated LUFS (BS.1770-4 gated). 400 ms blocks at 100 ms hop. ----
        if (resetIntegrated.exchange (false))
        {
            intBlocks.clear();
            for (auto& v : intRingMs) v = 0.0f;
            intRingFilled = 0;
            intRingNext   = 0;
            intHopAccumMs = 0.0f;
            intHopSampleN = 0;
            intHopRemaining = intHopSamples;
            integratedDb.store (-70.0f);
        }
        // Accumulate this block's K-weighted sum-of-squares (already in kSumSq).
        intHopAccumMs += (float) kSumSq;
        intHopSampleN += numS;
        intHopRemaining -= numS;
        if (intHopRemaining <= 0 && intHopSampleN > 0)
        {
            const float hopMs = intHopAccumMs / (float) intHopSampleN;
            intRingMs[intRingNext] = hopMs;
            intRingNext = (intRingNext + 1) & 3;
            if (intRingFilled < 4) ++intRingFilled;

            if (intRingFilled == 4)
            {
                // 400 ms block = mean of last four 100 ms hops.
                const float blockMs400 = 0.25f * (intRingMs[0] + intRingMs[1] + intRingMs[2] + intRingMs[3]);
                if (blockMs400 > 1.0e-12f)
                {
                    if ((int) intBlocks.size() >= kMaxBlocks)
                    {
                        // Decimate: keep every other block (preserves stats, halves storage).
                        std::size_t w = 0;
                        for (std::size_t r = 0; r < intBlocks.size(); r += 2)
                            intBlocks[w++] = intBlocks[r];
                        intBlocks.resize (w);
                    }
                    intBlocks.push_back (blockMs400);
                }

                // Gating: absolute -70 LUFS, then relative -10 LU.
                if (! intBlocks.empty())
                {
                    constexpr float absGateMs = 1.1724653045253207e-7f; // 10^((-70 + 0.691)/10)
                    double sum1 = 0.0; std::size_t n1 = 0;
                    for (auto m : intBlocks) if (m >= absGateMs) { sum1 += m; ++n1; }
                    if (n1 > 0)
                    {
                        const float meanAbs = (float) (sum1 / (double) n1);
                        const float meanLufs = -0.691f + 10.0f * std::log10 (meanAbs);
                        const float relGateLufs = meanLufs - 10.0f;
                        const float relGateMs = std::pow (10.0f, (relGateLufs + 0.691f) * 0.1f);
                        double sum2 = 0.0; std::size_t n2 = 0;
                        for (auto m : intBlocks)
                            if (m >= absGateMs && m >= relGateMs) { sum2 += m; ++n2; }
                        if (n2 > 0)
                        {
                            const float meanGated = (float) (sum2 / (double) n2);
                            const float intLufs = -0.691f + 10.0f * std::log10 (meanGated);
                            integratedDb.store (juce::jmax (-70.0f, intLufs));
                        }
                    }
                }
            }
            intHopAccumMs = 0.0f;
            intHopSampleN = 0;
            intHopRemaining += intHopSamples;
        }

        // Slow peak-hold logic (1.5 s flat, then fall ~12 dB/s).
        if (blockPeak >= slowHold)
        {
            slowHold       = blockPeak;
            slowHoldFrames = slowHoldReset;
        }
        else
        {
            slowHoldFrames -= numS;
            if (slowHoldFrames < 0)
            {
                // Drop ~12 dB/sec multiplicatively.
                slowHold *= std::pow (slowHoldFall, (float) numS);
                slowHoldFrames = 0;
            }
        }
        peakHoldDb.store (juce::Decibels::gainToDecibels (slowHold, -60.0f));

        if (clipped) clipLatched.store (true);
    }

    // -------- 4. True-peak (4x oversampled, channel-linked). --------
    // Lazy-init the oversampler on first call / channel-count or block-size
    // change to avoid vDSP setup storms during repeated prepare() calls.
    if (numCh > 0 && numS > 0)
    {
        if (truePeakOs == nullptr || truePeakOsCh != numCh || truePeakOsBlk < numS)
        {
            truePeakOsCh  = numCh;
            truePeakOsBlk = juce::nextPowerOfTwo (numS);
            truePeakOs = std::make_unique<juce::dsp::Oversampling<float>> (
                            (size_t) numCh, 2 /* 4x */,
                            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                            true /* maxQuality */, false);
            truePeakOs->initProcessing ((size_t) truePeakOsBlk);
            truePeakScratch.setSize (numCh, truePeakOsBlk, false, false, true);
        }

        // Copy into a scratch block sized to the prepared channel count.
        truePeakScratch.clear();
        for (int ch = 0; ch < numCh; ++ch)
            truePeakScratch.copyFrom (ch, 0, buffer, ch, 0, numS);

        juce::dsp::AudioBlock<float> inBlk (truePeakScratch.getArrayOfWritePointers(),
                                            (size_t) numCh, 0, (size_t) numS);
        auto upBlk = truePeakOs->processSamplesUp (inBlk);

        float blockTp = 0.0f;
        const auto upN = (int) upBlk.getNumSamples();
        for (size_t ch = 0; ch < upBlk.getNumChannels(); ++ch)
        {
            const auto* d = upBlk.getChannelPointer (ch);
            for (int s = 0; s < upN; ++s)
            {
                const float a = std::fabs (d[s]);
                if (a > blockTp) blockTp = a;
            }
        }
        if (blockTp > truePeakHold) truePeakHold = blockTp;
        truePeakHold *= std::pow (peakDecay, (float) numS);
        truePeakDb.store (juce::Decibels::gainToDecibels (truePeakHold, -60.0f));
    }
}
