#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ns
{
    enum class FxCategory
    {
        Gate,
        Compressor,
        Drive,
        EQ,
        Modulation,
        Delay,
        Reverb,
        Limiter,
        IRLoader,
        Utility,
        Other
    };

    inline const char* categoryName (FxCategory c)
    {
        switch (c)
        {
            case FxCategory::Gate:       return "GATE";
            case FxCategory::Compressor: return "COMP";
            case FxCategory::Drive:      return "DRIVE";
            case FxCategory::EQ:         return "EQ";
            case FxCategory::Modulation: return "MOD";
            case FxCategory::Delay:      return "DELAY";
            case FxCategory::Reverb:     return "REVERB";
            case FxCategory::Limiter:    return "LIMITER";
            case FxCategory::IRLoader:   return "IR";
            case FxCategory::Utility:    return "UTIL";
            case FxCategory::Other:      return "FX";
        }
        return "";
    }

    /** Reaper-style auto-classification using PluginDescription.category and name keywords. */
    inline FxCategory classifyPlugin (const juce::PluginDescription& d)
    {
        auto haystack = (d.category + " " + d.name + " " + d.descriptiveName + " "
                         + d.manufacturerName).toLowerCase();

        auto has = [&] (juce::StringRef k) { return haystack.contains (k); };

        // Order matters: more specific first.
        if (has ("noise gate") || has ("gate") || has ("expander"))
            return FxCategory::Gate;

        if (has ("limiter") || has ("brickwall") || has ("maximizer"))
            return FxCategory::Limiter;

        // Match IR / cabinet / convolution plugins BEFORE Drive (some cab sims
        // contain the word "amp") and BEFORE EQ (some IR loaders mention
        // filters). Common third-party names: NadIR, Cab Lab, Mixer IR,
        // OwnHammer Reverence, MConvolutionEZ, IR-1, Pulse, LeCab, Recabinet.
        if (has ("ir loader") || has ("impulse") || has ("convol")
            || has ("cab lab") || has ("cablab") || has ("nadir")
            || has ("recabinet") || has ("lecab") || has ("ownhammer")
            || has ("reverence") || has ("mixir") || has ("mixir2")
            || has ("ir-1") || has ("ir1") || has ("cab sim")
            || has ("speaker sim") || has ("cabinet"))
            return FxCategory::IRLoader;

        if (has ("compress") || has ("opto") || has ("1176")
            || has ("la-2a") || has ("la2a") || has ("ssl bus") || has ("vca comp"))
            return FxCategory::Compressor;

        if (has ("overdrive") || has ("distort") || has ("fuzz") || has ("drive")
            || has ("saturat") || has ("preamp") || has ("tube") || has ("boost")
            || has ("crunch") || has ("amp sim") || has ("tape"))
            return FxCategory::Drive;

        if (has ("equalizer") || has ("equaliser") || has ("eq ") || has (" eq")
            || has ("filter") || has ("parametric") || has ("graphic"))
            return FxCategory::EQ;

        if (has ("chorus") || has ("flanger") || has ("phaser") || has ("tremolo")
            || has ("vibrato") || has ("rotary") || has ("leslie") || has ("modulat")
            || has ("ensemble") || has ("uni-vibe") || has ("univibe"))
            return FxCategory::Modulation;

        if (has ("delay") || has ("echo"))
            return FxCategory::Delay;

        if (has ("reverb") || has ("hall") || has ("plate") || has ("spring")
            || has ("room") || has ("chamber"))
            return FxCategory::Reverb;

        if (has ("gain") || has ("trim") || has ("util") || has ("analyz")
            || has ("analys") || has ("meter") || has ("tuner"))
            return FxCategory::Utility;

        return FxCategory::Other;
    }
}
