#include "EQ.h"
#include <cmath>

void EQ::prepare (double sr, int bs)
{
    sampleRate = sr;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
    low .prepare (spec);
    peak.prepare (spec);
    high.prepare (spec);
    updateCoeffs();
}

void EQ::updateCoeffs()
{
    *low .state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sampleRate,  120.0f, 0.707f,
                                                                        juce::Decibels::decibelsToGain (bassDb.load()));
    *peak.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate,  800.0f, 0.707f,
                                                                        juce::Decibels::decibelsToGain (midDb.load()));
    *high.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 4000.0f, 0.707f,
                                                                        juce::Decibels::decibelsToGain (trebleDb.load()));
}

void EQ::process (juce::AudioBuffer<float>& buffer)
{
    if (dirty.exchange (false))
        updateCoeffs();

    // Bypass entirely when the user has not touched the tone EQ -- avoids
    // any biquad state / numerical-coefficient coloration on a signal that
    // is supposed to be pass-through.  (At exactly 0 dB the RBJ shelves /
    // peak collapse to unity coefficients mathematically, but skipping the
    // call is the only way to guarantee bit-identical output -- which is
    // what the user wants the app to feel like when nothing is engaged.)
    constexpr float kEps = 0.05f;
    if (std::abs (bassDb.load())   < kEps
     && std::abs (midDb.load())    < kEps
     && std::abs (trebleDb.load()) < kEps)
        return;

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    low .process (ctx);
    peak.process (ctx);
    high.process (ctx);
}
