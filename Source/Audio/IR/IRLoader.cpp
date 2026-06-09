#include "IRLoader.h"

void IRLoader::prepare (double sr, int bs)
{
    sampleRate = sr;
    blockSize  = bs;

    // Each slot is a single-channel convolution.
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
    for (auto& c : conv)
        c.prepare (spec);

    wetA.setSize (1, bs, false, false, true);
    wetB.setSize (1, bs, false, false, true);
    prepared = true;
}

bool IRLoader::loadIR (Slot slot, const juce::File& wav)
{
    if (! wav.existsAsFile())
        return false;

    conv[slot].loadImpulseResponse (wav,
                                    juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::yes,
                                    0,
                                    juce::dsp::Convolution::Normalise::yes);
    loaded[slot]      = true;
    currentFile[slot] = wav;
    return true;
}

void IRLoader::clearIR (Slot slot)
{
    conv[slot].reset();
    loaded[slot] = false;
    currentFile[slot] = juce::File();
}

void IRLoader::equalPowerPanGains (float pan, float& gL, float& gR) noexcept
{
    // Equal-power pan: pan in -1..+1, output gains sum to constant power.
    // Using sin/cos curve gives -3 dB at centre (industry standard).
    const float p = juce::jlimit (-1.0f, 1.0f, pan);
    const float angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // 0..pi/2
    gL = std::cos (angle);
    gR = std::sin (angle);
}

// ---------------------------------------------------------------------------
// Mono in-place: legacy single-slot path (Slot A only, to preserve identical
// tone when no Slot B is loaded). Centre-panned by definition.
// ---------------------------------------------------------------------------
void IRLoader::process (juce::AudioBuffer<float>& buffer)
{
    if (! prepared || ! loaded[SlotA])
        return;

    if (bypassed.load()) return; // dry passthrough — cab off

    const float wet = mix.load();
    if (wet <= 0.0f)
        return;

    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    conv[SlotA].process (ctx);

    // Wet-bus makeup gain compensates for Normalise::yes (see header).
    const float makeup = juce::Decibels::decibelsToGain (makeupDb.load());
    if (makeup != 1.0f)
        buffer.applyGain (makeup);

    if (wet < 1.0f)
    {
        const float dryAmt = 1.0f - wet;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            buffer.applyGain (ch, 0, buffer.getNumSamples(), wet);
            buffer.addFrom  (ch, 0, dry, ch, 0, buffer.getNumSamples(), dryAmt);
        }
    }
}

// ---------------------------------------------------------------------------
// Stereo render: convolve mono input through each loaded slot independently,
// pan each to its slot pan, sum into the stereo output buffer. Replaces the
// engine's manual mono->stereo upmix.
// ---------------------------------------------------------------------------
void IRLoader::processToStereo (const juce::AudioBuffer<float>& monoIn,
                                juce::AudioBuffer<float>& stereoOut)
{
    if (! prepared || stereoOut.getNumChannels() < 2)
        return;

    const int n = juce::jmin (monoIn.getNumSamples(), stereoOut.getNumSamples());
    if (n <= 0) return;

    // Make sure scratch buffers are large enough (host can change block size).
    if (wetA.getNumSamples() < n) wetA.setSize (1, n, false, false, true);
    if (wetB.getNumSamples() < n) wetB.setSize (1, n, false, false, true);

    const float wet = mix.load();
    // Wet-bus makeup gain compensates for Normalise::yes (see header).
    const float makeup = juce::Decibels::decibelsToGain (makeupDb.load());

    auto* L = stereoOut.getWritePointer (0);
    auto* R = stereoOut.getWritePointer (1);
    const float* dryMono = monoIn.getReadPointer (0);

    // Start with the dry signal mirrored to L/R (so that mix < 1.0 keeps
    // the un-cab'd amp signal audible — matches process()'s behaviour).
    juce::FloatVectorOperations::copy (L, dryMono, n);
    juce::FloatVectorOperations::copy (R, dryMono, n);

    if (bypassed.load()) return; // dry-stereo passthrough — cab off

    if (wet <= 0.0f) return;

    auto renderSlot = [this, dryMono, n, L, R, wet, makeup] (Slot s, juce::AudioBuffer<float>& wetBuf)
    {
        if (! loaded[s]) return;

        // Copy mono dry into wet scratch, run convolution in place.
        juce::FloatVectorOperations::copy (wetBuf.getWritePointer (0), dryMono, n);
        juce::AudioBuffer<float> view (wetBuf.getArrayOfWritePointers(), 1, n);
        juce::dsp::AudioBlock<float> block (view);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        conv[s].process (ctx);

        // Apply equal-power pan and sum onto the (already-dry) L/R buffers,
        // first scaling existing dry by (1 - wet) so the wet/dry mix balances.
        // We do this only once, on the first slot rendered, then the second
        // slot just adds. Track first-slot via a static-like check below.
        float gL = 1.0f, gR = 1.0f;
        equalPowerPanGains (pan[s].load(), gL, gR);
        const float* w = wetBuf.getReadPointer (0);
        juce::FloatVectorOperations::addWithMultiply (L, w, wet * makeup * gL, n);
        juce::FloatVectorOperations::addWithMultiply (R, w, wet * makeup * gR, n);
    };

    // Apply dry attenuation first so the wet adds make sense.
    if (wet < 1.0f)
    {
        const float dryGain = 1.0f - wet;
        juce::FloatVectorOperations::multiply (L, dryGain, n);
        juce::FloatVectorOperations::multiply (R, dryGain, n);
    }
    else
    {
        // wet = 1.0 → kill dry entirely (matches mono path behaviour).
        juce::FloatVectorOperations::clear (L, n);
        juce::FloatVectorOperations::clear (R, n);
    }

    renderSlot (SlotA, wetA);
    renderSlot (SlotB, wetB);
}
