#include "NAMProcessor.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include "NamForceLink.h"   // unconditional: NamRegistrationGuard (below) always needs it

#if NS_HAVE_NAM_CORE
 #include "NAM/dsp.h"
 #include "NAM/get_dsp.h"
 #include "NAM/slimmable.h"
 #include "ResamplingNAM.h"
#endif

// File-scope guard: ensures SlimmableContainer is in ConfigParserRegistry
// before the first loadSlot() -> get_dsp() call. The struct constructor has
// an observable side effect (external function call) so the compiler cannot
// elide it, and it runs before any NAMProcessor instance is constructed.
namespace {
    struct NamRegistrationGuard {
        NamRegistrationGuard() noexcept { ns_ensureNamContainerRegistered(); }
    };
    static const NamRegistrationGuard _s_namRegistrationGuard;
}

NAMProcessor::NAMProcessor()
{
    ns_ensureNamContainerRegistered();

    for (auto& w : weights)       w.store (0.0f);
    for (auto& g : normGainLinear) g.store (1.0f);
    for (auto& s : slimSize)       s.store (1.0f); // 1.0 = full quality
    for (auto& b : slotBypassed)   b.store (false);
    weights[0].store (1.0f); // sane default: slot A fully on if loaded
}

NAMProcessor::~NAMProcessor() = default;

void NAMProcessor::prepare (double sr, int blockSize)
{
    sampleRate = sr;
    maxBlock   = blockSize;

    // The NAM models run at their NATIVE training sample rate via the per-slot
    // ResamplingNAM wrappers (see loadSlot). There is no global oversampling --
    // that is exactly what keeps the tone identical to Steve Atkinson's
    // reference plugin. Re-Reset every loaded model so its internal resampler
    // matches the new session SR / block size.
    useOversampling = false;
    oversampler.reset();

   #if NS_HAVE_NAM_CORE
    for (int i = 0; i < kNumSlots; ++i)
        if (auto m = std::atomic_load (&models[(size_t) i]))
            m->Reset (sr, blockSize);
   #endif

    dryIn  .setSize (1, blockSize, false, false, true);
    slotOut.setSize (1, blockSize, false, false, true);
    mixOut .setSize (1, blockSize, false, false, true);

    // 20 ms ramp on each slot weight — click-free slot switching / blend moves.
    for (int i = 0; i < kNumSlots; ++i)
    {
        smoothedWeights[(size_t) i].reset (sr, 0.020);
        smoothedWeights[(size_t) i].setCurrentAndTargetValue (weights[(size_t) i].load());
    }

    // Hosted amp-sim plugin: re-prepare at the new SR / block size. We do this
    // synchronously here since prepare() is called from the message thread
    // (or audio-device-change callback) before audio starts.
    if (auto hp = std::atomic_load (&hostedPlugin))
    {
        hp->setPlayConfigDetails (2, 2, sr, blockSize);
        hp->prepareToPlay (sr, blockSize);
    }
    hostedStereo.setSize (2, juce::jmax (blockSize, 64), false, false, true);
}

bool NAMProcessor::loadSlot (int slot, const juce::File& file, juce::String& errorOut)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
    {
        errorOut = "Invalid slot index";
        return false;
    }

   #if NS_HAVE_NAM_CORE
    ns_ensureNamContainerRegistered(); // final guarantee before get_dsp() parses architecture
    try
    {
        auto loaded = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
        if (loaded == nullptr) { errorOut = "NAM: returned null DSP"; return false; }

        // Cache loudness makeup gain so process() can normalize each model to
        // a fixed reference (-18 dBu by default), matching the reference
        // plugin's "Normalized" output mode. Read this from the bare model
        // BEFORE it is moved into the resampling wrapper.
        float makeup = 1.0f;
        try
        {
            if (loaded->HasLoudness())
            {
                const double loud = loaded->GetLoudness();
                makeup = juce::Decibels::decibelsToGain (
                            (float) ((double) kReferenceDbu - loud));
            }
        }
        catch (...) { makeup = 1.0f; }
        normGainLinear[(size_t) slot].store (makeup);

        // Store the MODEL's native training sample rate (NOT the session SR)
        // so the UI can show it. Falls back to 48 kHz (the historical NAM
        // default) when the model carries no metadata.
        double modelSr = loaded->GetExpectedSampleRate();
        if (modelSr <= 0.0) modelSr = 48000.0;

        // Wrap the model in a ResamplingNAM so it always runs at its native
        // training rate -- the audio is resampled to/from that rate exactly as
        // in Steve Atkinson's reference plugin. This is what restores the
        // clarity and low-end that fixed oversampling was altering.
        const double prepSr = (sampleRate > 0.0) ? sampleRate : 48000.0;
        auto wrapped = std::make_shared<ResamplingNAM> (std::move (loaded), prepSr);
        if (maxBlock > 0)
            wrapped->Reset (prepSr, maxBlock);

        std::shared_ptr<nam::DSP> shared = wrapped;
        std::atomic_store (&models[(size_t) slot], shared);

        slotMeta[(size_t) slot].filePath    = file;
        slotMeta[(size_t) slot].displayName = file.getFileNameWithoutExtension();
        slotMeta[(size_t) slot].sampleRate  = modelSr;

        // Always clear bypass when a file is explicitly loaded — a previously
        // recalled scene may have stored a garbage bypass=true value when the
        // slotBypassed array was uninitialised in older builds.
        slotBypassed[(size_t) slot].store (false);

        // Propagate the stored slim-size setting to the newly loaded model so
        // A2 (SlimmableContainer) models honour the UI slider value immediately.
        setSlimSize (slot, slimSize[(size_t) slot].load());

        // Re-derive blend weights against the now-loaded slot set so the
        // current XY position is honoured immediately (otherwise the new
        // slot would sit at zero until the dot is moved).
        setXYBlend (xyBlendX.load(), xyBlendY.load());

        // Signal the audio thread to snap smoothers to the new weights on
        // the very next block. Without this, smoothers keep their old values
        // (often 0.0) until the ramp catches up — audible as silence on A/B/C
        // at startup when prepare() ran before the models were loaded.
        triggerSmoothedWeightsReinit();
        return true;
    }
    catch (const std::exception& e)
    {
        errorOut = juce::String ("NAM load failed: ") + e.what();
        return false;
    }
   #else
    juce::ignoreUnused (file);
    errorOut = "NAM core not compiled in.";
    return false;
   #endif
}

void NAMProcessor::clearSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return;
   #if NS_HAVE_NAM_CORE
    std::atomic_store (&models[(size_t) slot], std::shared_ptr<nam::DSP>{});
   #endif
    normGainLinear[(size_t) slot].store (1.0f);
    slotMeta[(size_t) slot] = {};

    // Redistribute weights across whatever slots remain loaded.
    setXYBlend (xyBlendX.load(), xyBlendY.load());
}

bool NAMProcessor::hasSlot (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return false;
   #if NS_HAVE_NAM_CORE
    return std::atomic_load (&models[(size_t) slot]) != nullptr;
   #else
    return false;
   #endif
}

juce::String NAMProcessor::getSlotName (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return {};
    return slotMeta[(size_t) slot].displayName;
}

double NAMProcessor::getSlotSampleRate (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return 0.0;
    return slotMeta[(size_t) slot].sampleRate;
}

bool NAMProcessor::hasAnySampleRateMismatch() const noexcept
{
    const double sessionSr = sampleRate;
    if (sessionSr <= 0.0) return false;
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (! hasSlot (i)) continue;
        const double modelSr = slotMeta[(size_t) i].sampleRate;
        if (modelSr > 0.0 && std::abs (modelSr - sessionSr) > 1.0)
            return true;
    }
    return false;
}

int NAMProcessor::getLatencySamples() const noexcept
{
   #if NS_HAVE_NAM_CORE
    // Each loaded model contributes its resampler's latency (0 when the model
    // already runs at the session SR -- the common case). Report the largest
    // so the DAW's PDC aligns the slowest slot.
    int maxLat = 0;
    for (int i = 0; i < kNumSlots; ++i)
        if (auto m = std::atomic_load (&models[(size_t) i]))
            if (auto* rn = dynamic_cast<ResamplingNAM*> (m.get()))
                maxLat = juce::jmax (maxLat, rn->GetLatency());
    return maxLat;
   #else
    return 0;
   #endif
}

void NAMProcessor::setSlotWeight (int slot, float w) noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return;
    weights[(size_t) slot].store (juce::jlimit (0.0f, 1.0f, w));
}

float NAMProcessor::getSlotWeight (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return 0.0f;
    return weights[(size_t) slot].load();
}

//==============================================================================
// Per-slot slim (slimmable A2 models), bypass, and input trim.
//==============================================================================
#if NS_HAVE_NAM_CORE
namespace
{
    // Reach the nam::SlimmableModel interface through the ResamplingNAM wrapper.
    nam::SlimmableModel* asSlimmable (const std::shared_ptr<nam::DSP>& m) noexcept
    {
        if (m == nullptr) return nullptr;
        nam::DSP* inner = m.get();
        if (auto* rn = dynamic_cast<ResamplingNAM*> (m.get()))
            inner = rn->GetEncapsulated();
        return dynamic_cast<nam::SlimmableModel*> (inner);
    }
}
#endif

void NAMProcessor::setSlimSize (int slot, float value) noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return;
    const float v = juce::jlimit (0.0f, 1.0f, value);
    slimSize[(size_t) slot].store (v);
   #if NS_HAVE_NAM_CORE
    if (auto* s = asSlimmable (std::atomic_load (&models[(size_t) slot])))
        s->SetSlimmableSize ((double) v);
   #endif
}

float NAMProcessor::getSlimSize (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return 1.0f;
    return slimSize[(size_t) slot].load();
}

bool NAMProcessor::isSlotSlimmable (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return false;
   #if NS_HAVE_NAM_CORE
    return asSlimmable (std::atomic_load (&models[(size_t) slot])) != nullptr;
   #else
    return false;
   #endif
}

void NAMProcessor::setSlotBypassed (int slot, bool b) noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return;
    slotBypassed[(size_t) slot].store (b);
}

bool NAMProcessor::isSlotBypassed (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return false;
    return slotBypassed[(size_t) slot].load();
}

void NAMProcessor::setSlotInputTrimDb (int slot, float db) noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return;
    slotInputTrimDb[(size_t) slot].store (juce::jlimit (-24.0f, 24.0f, db));
}

float NAMProcessor::getSlotInputTrimDb (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots)) return 0.0f;
    return slotInputTrimDb[(size_t) slot].load();
}

void NAMProcessor::setXYBlend (float x, float y) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);
    xyBlendX.store (x);
    xyBlendY.store (y);

    // Anchors at the four edge midpoints:
    //   slot 0 = A = top    (0.5, 0.0)
    //   slot 1 = B = bottom (0.5, 1.0)
    //   slot 2 = C = left   (0.0, 0.5)
    //   slot 3 = D = right  (1.0, 0.5)
    constexpr float ax[4] = { 0.5f, 0.5f, 0.0f, 1.0f };
    constexpr float ay[4] = { 0.0f, 1.0f, 0.5f, 0.5f };

    // Hat / plateau function with equal-power normalisation:
    //   d <= 0.5 (anchor to centre)  → hat = 1.0  (full relative weight)
    //   0.5 < d <= 1.0               → hat fades linearly 1 → 0
    //   d > 1.0                      → hat = 0
    //
    // Each anchor is exactly 0.5 units from the centre, so at centre every
    // loaded slot has hat = 1.0.  After computing hats we normalise by the
    // RMS so the sum-of-squares = 1.0 at all positions — this keeps total
    // output level constant regardless of how many models are active,
    // preventing the loudness increase / harshness that occurs when multiple
    // NAMs are summed at full gain.
    //   2 models blended equally:  each weight = 1/√2 ≈ 0.707
    //   4 models blended equally:  each weight = 1/√4 = 0.5
    constexpr float kFadeStart = 0.5f;

    float hat[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float sumSq  = 0.0f;

    for (int i = 0; i < 4; ++i)
    {
       #if NS_HAVE_NAM_CORE
        if (std::atomic_load (&models[(size_t) i]) == nullptr) continue;
       #endif
        const float dx = x - ax[i];
        const float dy = y - ay[i];
        const float d  = std::sqrt (dx * dx + dy * dy);
        hat[i] = (d <= kFadeStart)
                     ? 1.0f
                     : juce::jmax (0.0f, 1.0f - (d - kFadeStart) / kFadeStart);
        sumSq += hat[i] * hat[i];
    }

    if (sumSq > 0.0f)
    {
        const float norm = 1.0f / std::sqrt (sumSq);
        for (int i = 0; i < 4; ++i)
            weights[(size_t) i].store (hat[i] * norm);
    }
    else
    {
        for (int i = 0; i < 4; ++i) weights[(size_t) i].store (0.0f);
    }
    // NOTE: we deliberately do NOT call triggerSmoothedWeightsReinit() here.
    // setXYBlend() is called on every blend-pad drag event; snapping smoothers
    // on each event cancels the 20 ms crossfade and causes a click per mouse
    // movement.  Reinit is triggered by loadSlot() (first model load / model
    // swap) and by the App.cpp startup safety net.  The self-healing block in
    // process() covers any residual stuck-at-zero cases.
}

void NAMProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int n  = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    if (nc <= 0 || n <= 0) return;

    if (bypassed.load()) return; // dry passthrough — amp off

    // Snap smoothers to current weights if requested (e.g. right after loadSlot
    // at startup, before the 20 ms ramp has time to bring them up from 0).
    if (reinitSmoothedWeightsPending.exchange (false, std::memory_order_acq_rel))
        for (int i = 0; i < kNumSlots; ++i)
            smoothedWeights[(size_t) i].setCurrentAndTargetValue (weights[(size_t) i].load());

    // Self-healing safety net: if a loaded slot's smoother is still at (or
    // near) zero while its target weight is non-zero, snap it immediately.
    // This intentionally fires even when the smoother IS in mid-ramp
    // (isSmoothing == true): if getCurrentValue() is still < 0.001 the ramp
    // has barely started and any block-by-block reset (e.g. setTargetValue
    // called with 0 from a stale weights[] read) could keep the slot
    // permanently silent.  Snapping is safe here because the slot is
    // effectively producing no audio yet (weight ≈ 0).
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& sm = smoothedWeights[(size_t) i];
        const float tw = slotBypassed[(size_t) i].load()
                             ? 0.0f : weights[(size_t) i].load();
        if (tw > 0.001f
            && sm.getCurrentValue() < 0.001f
            && std::atomic_load (&models[(size_t) i]) != nullptr)
            sm.setCurrentAndTargetValue (tw);
    }

    // Hosted amp-sim plugin takes priority over NAM models. If one is
    // loaded, run the input mono signal through the plugin as a 2-in / 2-out
    // process call and sum L+R back to mono. NAM model slots are bypassed in
    // this mode (their weights / loaded state are preserved on disk, just
    // muted from the routing).
    if (auto hp = std::atomic_load (&hostedPlugin))
    {
        if (hostedStereo.getNumSamples() < n)
            hostedStereo.setSize (2, n, false, false, true);

        const float pre  = juce::Decibels::decibelsToGain (preGainDb.load());
        const float post = juce::Decibels::decibelsToGain (postGainDb.load());

        // Duplicate mono input -> stereo plugin input, apply pre-gain.
        const float* src = buffer.getReadPointer (0);
        float* l = hostedStereo.getWritePointer (0);
        float* r = hostedStereo.getWritePointer (1);
        juce::FloatVectorOperations::copyWithMultiply (l, src, pre, n);
        juce::FloatVectorOperations::copy            (r, l, n);

        // Build a stereo buffer that references our scratch so the plugin
        // sees exactly n samples even if hostedStereo is over-allocated.
        float* chans[2] = { l, r };
        juce::AudioBuffer<float> pluginBuf (chans, 2, n);
        hostedMidi.clear();
        hp->processBlock (pluginBuf, hostedMidi);

        // Sum back to mono into the original buffer and apply post-gain.
        float* dst = buffer.getWritePointer (0);
        const float* pl = pluginBuf.getReadPointer (0);
        const float* pr = pluginBuf.getReadPointer (1);
        for (int i = 0; i < n; ++i)
            dst[i] = 0.5f * (pl[i] + pr[i]) * post;

        // Re-broadcast to any extra channels.
        for (int c = 1; c < nc; ++c)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (c), dst, n);
        return;
    }

   #if NS_HAVE_NAM_CORE
    // Fast pass-through when nothing is loaded. Without this the audio
    // still round-trips through the 2x oversampler (HQ polyphase IIR/FIR
    // chain) which attenuates by ~0.5–1 dB at full bandwidth and leaves a
    // faint amplitude ripple — audible as "low volume, slightly wavey"
    // DI even with no amp model selected.
    {
        bool anyActive = false;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (weights[(size_t) i].load() > 0.0001f
                && std::atomic_load (&models[(size_t) i]) != nullptr)
            { anyActive = true; break; }
        }
        if (! anyActive)
        {
            // Apply the DI monitor makeup so the dry signal sits at a
            // perceptually sensible level (default +12 dB — set to 0 dB
            // for bit-exact bypass). Without makeup, a raw DI sounds much
            // quieter than a typical loaded amp patch and users perceive
            // the plugin as "too quiet".
            const float mk = juce::Decibels::decibelsToGain (dryMakeupDb.load());
            if (mk != 1.0f) buffer.applyGain (mk);
            return;
        }
    }
   #endif

    const float pre  = juce::Decibels::decibelsToGain (preGainDb.load());
    const float post = juce::Decibels::decibelsToGain (postGainDb.load());

    buffer.applyGain (pre);

   #if NS_HAVE_NAM_CORE
    // Run every active slot at the SESSION rate. Each model is wrapped in a
    // ResamplingNAM that internally resamples to/from its own native training
    // rate, so this blend is always sample-aligned at the session rate -- and
    // every model hears the rate it was trained at, exactly like the reference.
    // Separate input (dryIn) and output (slotOut) buffers: the resampler
    // requires non-aliased in/out.
    if (dryIn  .getNumSamples() < n) dryIn  .setSize (1, n, false, false, true);
    if (slotOut.getNumSamples() < n) slotOut.setSize (1, n, false, false, true);
    if (mixOut .getNumSamples() < n) mixOut .setSize (1, n, false, false, true);

    juce::FloatVectorOperations::copy (dryIn.getWritePointer (0),
                                       buffer.getReadPointer (0), n);
    mixOut.clear (0, 0, n);

    {
        const bool normalize = normalizeEnabled.load();
        bool anyActive = false;
        float* const in0base = dryIn.getWritePointer (0);   // shared input (read-only to models)
        float* const dst     = mixOut.getWritePointer (0);

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto& sm = smoothedWeights[(size_t) i];
            // A bypassed slot smooths its blend weight to 0 (click-free); the
            // model still processes below so its internal state stays warm.
            const float rawTarget = slotBypassed[(size_t) i].load()
                                        ? 0.0f : weights[(size_t) i].load();
            sm.setTargetValue (rawTarget);

            const float w        = sm.getCurrentValue();
            const bool  smoothing = sm.isSmoothing();

            // Skip only when BOTH the smoother AND the raw weight target are
            // effectively zero.  Checking rawTarget alone prevents a stuck-at-0
            // smoother (startup race between prepare() and loadSlot()) from
            // permanently silencing a slot whose blend weight is already correct.
            if (! smoothing && w <= 0.0001f && rawTarget <= 0.0001f) continue;

            auto m = std::atomic_load (&models[(size_t) i]);
            if (m == nullptr) { sm.skip (n); continue; }

            float* in0  = in0base;
            float* out0 = slotOut.getWritePointer (0);
            m->process (&in0, &out0, n);

            const float makeup = normalize ? normGainLinear[(size_t) i].load() : 1.0f;

            // Use the smoother for smooth crossfades; fall back to rawTarget
            // if the smoother hasn't caught up yet (stuck-at-zero startup case).
            const float mixW = (w < 0.001f && rawTarget > 0.001f) ? rawTarget : w;

            if (! smoothing)
            {
                juce::FloatVectorOperations::addWithMultiply (dst, out0, mixW * makeup, n);
            }
            else
            {
                for (int s = 0; s < n; ++s)
                    dst[s] += out0[s] * sm.getNextValue() * makeup;
            }
            anyActive = true;
        }

        if (anyActive)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (0),
                                               mixOut.getReadPointer (0), n);
    }

    // Broadcast mono ch0 to any additional channels.
    for (int ch = 1; ch < nc; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch),
                                           buffer.getReadPointer (0), n);
   #endif

    buffer.applyGain (post);
}

//==============================================================================
// Hosted amp-sim plugin
//==============================================================================
bool NAMProcessor::setHostedPlugin (std::unique_ptr<juce::AudioPluginInstance> plugin,
                                    const juce::String& displayName)
{
    if (plugin == nullptr) return false;

    // Configure the plugin for stereo 2-in / 2-out at our current SR + block
    // size. Most amp-sim plugins (Archetype, Bias, ML Sound Lab, etc.) are
    // happy with this layout; for the few that prefer mono-in / stereo-out we
    // still feed them L=R so the result is identical to mono input.
    plugin->setPlayConfigDetails (2, 2, sampleRate, maxBlock);
    plugin->prepareToPlay (sampleRate, juce::jmax (maxBlock, 64));
    plugin->setNonRealtime (false);
    plugin->setPlayHead (nullptr); // host clock plumbed elsewhere if needed

    // Custom deleter calls releaseResources() before destruction so the
    // plugin tears down cleanly even when the last shared_ptr happens to
    // drop on the audio thread (rare race during clearHostedPlugin while a
    // block is mid-flight).
    std::shared_ptr<juce::AudioPluginInstance> sp (
        plugin.release(),
        [] (juce::AudioPluginInstance* p)
        {
            if (p == nullptr) return;
            p->releaseResources();
            delete p;
        });

    {
        const juce::ScopedLock sl (hostedNameLock);
        hostedDisplayName = displayName;
    }
    std::atomic_store (&hostedPlugin, sp);
    return true;
}

void NAMProcessor::clearHostedPlugin()
{
    // Close the editor window first so the plugin teardown doesn't pull the
    // rug from under an open editor component.
    hostedEditorWindow.reset();

    std::shared_ptr<juce::AudioPluginInstance> empty;
    std::atomic_store (&hostedPlugin, empty);

    const juce::ScopedLock sl (hostedNameLock);
    hostedDisplayName.clear();
}

bool NAMProcessor::hasHostedPlugin() const noexcept
{
    return std::atomic_load (&hostedPlugin) != nullptr;
}

juce::String NAMProcessor::getHostedPluginName() const
{
    const juce::ScopedLock sl (hostedNameLock);
    return hostedDisplayName;
}

juce::AudioPluginInstance* NAMProcessor::getHostedPluginInstance() const noexcept
{
    auto sp = std::atomic_load (&hostedPlugin);
    return sp.get();
}

juce::PluginDescription NAMProcessor::getHostedPluginDescription() const
{
    juce::PluginDescription d;
    if (auto sp = std::atomic_load (&hostedPlugin))
        sp->fillInPluginDescription (d);
    return d;
}
