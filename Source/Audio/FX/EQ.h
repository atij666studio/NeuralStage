#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

/** Simple 3-band tone EQ (placeholder shelves/peak). */
class EQ
{
public:
    void prepare (double sampleRate, int blockSize);
    void process (juce::AudioBuffer<float>& buffer);

    void setBass   (float db) noexcept { bassDb.store (db);   dirty = true; }
    void setMid    (float db) noexcept { midDb.store (db);    dirty = true; }
    void setTreble (float db) noexcept { trebleDb.store (db); dirty = true; }

    float getBass()   const noexcept { return bassDb.load();   }
    float getMid()    const noexcept { return midDb.load();    }
    float getTreble() const noexcept { return trebleDb.load(); }

private:
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;
    Filter low, peak, high;

    std::atomic<float> bassDb { 0.0f }, midDb { 0.0f }, trebleDb { 0.0f };
    std::atomic<bool>  dirty  { true };
    double sampleRate { 48000.0 };

    void updateCoeffs();
};
