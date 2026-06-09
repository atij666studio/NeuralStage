#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

class OutputProcessor
{
public:
    void prepare (double sampleRate, int blockSize);
    void process (juce::AudioBuffer<float>& buffer);

    void setPostGainDb (float db) noexcept { postGainDb.store (db); }
    float getPostGainDb() const noexcept   { return postGainDb.load(); }

    /** Independent per-scene loudness trim (dB). Added to postGainDb when
     *  computing the output level. Default 0 dB. SceneManager writes this
     *  on recall so users can level-match scenes without touching their
     *  global output gain. Range clamped to [-24, +24] dB. */
    void  setSceneTrimDb (float db) noexcept { sceneTrimDb.store (juce::jlimit (-24.0f, 24.0f, db)); }
    float getSceneTrimDb() const noexcept    { return sceneTrimDb.load(); }

    /** A/B compare loudness-match trim (dB). Independent of scene trim;
     *  the App's A/B logic writes this. Range clamped to [-24, +24]. */
    void  setAbTrimDb (float db) noexcept { abTrimDb.store (juce::jlimit (-24.0f, 24.0f, db)); }
    float getAbTrimDb() const noexcept    { return abTrimDb.load(); }

    void setMute (bool b) noexcept { muted.store (b); }
    bool isMuted() const noexcept  { return muted.load(); }

    /** Trigger a short click-masking output duck. Call from the message thread
     *  on every scene recall. The audio thread fades the output to silence
     *  over ~3 ms, holds ~10 ms while the chain / NAM-model swap settles, then
     *  fades back over ~28 ms. Guarantees a silent, near-instant scene change
     *  regardless of what differs between the two scenes (different FX chains,
     *  IRs or NAM models that would otherwise pop). Real-time safe. */
    void notifySceneSwitch() noexcept { sceneSwitchGen.fetch_add (1); }

    // Last-line-of-defence brickwall (sample-peak, channel-linked).
    // Enabled by default; -0.3 dBFS ceiling. Catches profile blow-ups.
    void  setSafetyLimiterEnabled (bool b) noexcept { limiterOn.store (b); }
    bool  isSafetyLimiterEnabled() const noexcept   { return limiterOn.load(); }
    void  setSafetyCeilingDb (float db) noexcept    { ceilingDb.store (db); }
    float getSafetyCeilingDb() const noexcept       { return ceilingDb.load(); }
    // Last-block max gain-reduction in dB (UI meter); 0 = no reduction.
    float getSafetyReductionDb() const noexcept     { return lastReductionDb.load(); }

    // Post-gain output level meters (peak in dBFS, decaying; channel-linked).
    // Returns 0 dBFS = full-scale, -inf-ish (clamped to -60) when silent.
    float getOutputPeakDb() const noexcept { return peakDb.load(); }
    float getOutputRmsDb()  const noexcept { return rmsDb.load();  }

    /** Short-term loudness in LUFS (ITU-R BS.1770 K-weighted, ~3 s EMA window).
     *  Used by the A/B "loudness-match" feature. */
    float getOutputLoudnessDb() const noexcept { return loudnessDb.load(); }

    /** Integrated loudness in LUFS (BS.1770-4 gated: absolute -70 LUFS gate +
     *  relative -10 LU gate). Updates every 100 ms hop; reflects the entire
     *  measurement window since the last reset. */
    float getIntegratedLoudnessDb() const noexcept { return integratedDb.load(); }

    /** Reset the integrated-loudness measurement (clears block history). */
    void  resetIntegratedLoudness() noexcept { resetIntegrated.store (true); }

    /** True-peak in dBTP (ITU-R BS.1770 4× oversampled, channel-linked).
     *  Decays like the regular peak meter. -inf clamped to -60 dBTP. */
    float getOutputTruePeakDb() const noexcept { return truePeakDb.load(); }

    // Slow peak-hold (~1.5 s plateau then drops). Useful for a static dot/marker.
    float getOutputPeakHoldDb() const noexcept { return peakHoldDb.load(); }

    // Latched clip indicator. Set true the moment any sample hits +/- 1.0 (post
    // safety limiter, so it should be very rare). Stays latched until cleared.
    bool  hasClipped() const noexcept { return clipLatched.load(); }
    void  clearClip() noexcept        { clipLatched.store (false); }

private:
    std::atomic<float> postGainDb { 0.0f };
    std::atomic<float> sceneTrimDb { 0.0f };
    std::atomic<float> abTrimDb { 0.0f };
    std::atomic<bool>  muted { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain { 1.0f };

    // Scene-switch click guard. notifySceneSwitch() bumps sceneSwitchGen from
    // the message thread; the audio thread runs a fade-out -> hold -> fade-in
    // envelope (duckGain) over the swap so chain / model discontinuities are
    // masked. duckPhase: 0 = idle, 1 = fade out, 2 = hold, 3 = fade in.
    std::atomic<int> sceneSwitchGen { 0 };
    int   duckSeenGen { 0 };
    int   duckPhase   { 0 };
    float duckGain    { 1.0f };
    int   duckHoldRem { 0 };
    int   duckHoldLen { 0 };
    float duckOutStep { 1.0f };
    float duckInStep  { 1.0f };

    // Safety limiter state.
    std::atomic<bool>  limiterOn { true };
    std::atomic<float> ceilingDb { -0.3f };
    std::atomic<float> lastReductionDb { 0.0f };
    float envelope    { 1.0f };  // current gain-reduction multiplier (1 = no reduction)
    float attackCoef  { 0.0f };
    float releaseCoef { 0.0f };

    // Output meter state (peak with ~300 ms decay, RMS over a small window).
    std::atomic<float> peakDb { -60.0f };
    std::atomic<float> rmsDb  { -60.0f };
    float peakHold   { 0.0f };
    float peakDecay  { 0.0f };  // multiplicative per-sample decay

    // Long-window EMA of K-weighted mean-square for short-term loudness (~3 s).
    // ITU-R BS.1770: two biquads per channel (high-shelf pre-filter + RLB HPF),
    // mean-square of the filtered signal, channel-summed (L+R weights = 1).
    static constexpr int kMaxLoudnessCh = 8;
    struct Biquad { float b0=0,b1=0,b2=0,a1=0,a2=0; float x1=0,x2=0,y1=0,y2=0; };
    Biquad kPre [kMaxLoudnessCh];
    Biquad kRlb [kMaxLoudnessCh];
    std::atomic<float> loudnessDb { -70.0f };
    float loudnessMs   { 0.0f };  // running K-weighted, channel-summed mean-square
    double loudnessTau { 3.0 };   // seconds
    double loudnessSr  { 48000.0 };

    // Integrated loudness (BS.1770-4 gated). 400 ms blocks at 100 ms hop;
    // gates at -70 LUFS absolute and -10 LU relative to ungated mean.
    // Per-block sum-of-MS values are kept in a bounded vector — when full
    // (>kMaxBlocks), we decimate by keeping every other block (preserves
    // long-window statistics for arbitrarily long sets without unbounded RAM).
    std::atomic<float> integratedDb  { -70.0f };
    std::atomic<bool>  resetIntegrated { false };
    int   intHopSamples   { 0 };   // samples per 100 ms hop
    int   intHopRemaining { 0 };   // samples until next hop boundary
    float intRingMs[4]    { 0,0,0,0 }; // last 4 hops of K-weighted MS (= 400 ms block when summed)
    int   intRingFilled   { 0 };
    int   intRingNext     { 0 };
    float intHopAccumMs   { 0.0f };  // K-weighted MS accumulator within current hop
    int   intHopSampleN   { 0 };     // samples accumulated within current hop
    static constexpr int kMaxBlocks = 8192; // ~13 min @ 100 ms hop after one decimation pass; halves indefinitely
    std::vector<float> intBlocks; // per-block sum-of-MS values

    // True-peak (4× oversampled, channel-linked, peak-decay matches sample peak).
    std::unique_ptr<juce::dsp::Oversampling<float>> truePeakOs;
    int   truePeakOsCh   { 0 };
    int   truePeakOsBlk  { 0 };
    juce::AudioBuffer<float> truePeakScratch;
    std::atomic<float> truePeakDb { -60.0f };
    float truePeakHold { 0.0f };

    // Slow peak-hold marker (~1.5 s flat hold, then fast fall).
    std::atomic<float> peakHoldDb { -60.0f };
    float slowHold       { 0.0f };
    int   slowHoldFrames { 0 };
    int   slowHoldReset  { 0 };  // samples to keep the hold flat
    float slowHoldFall   { 1.0f }; // multiplicative per-sample fall (~12 dB/s)

    // Clip latch (any sample == +/- 1.0 after limiter == genuine overload).
    std::atomic<bool> clipLatched { false };
};
