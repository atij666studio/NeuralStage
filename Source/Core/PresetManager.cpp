#include "PresetManager.h"
#include "../App.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/NAM/NAMProcessor.h"
#include "../PluginHost/PluginManager.h"
#include "../PluginHost/PluginChain.h"
#include "../Utils/FileUtils.h"

namespace
{
    const juce::Identifier kRoot       { "NeuralStagePreset" };
    const juce::Identifier kVersion    { "version" };
    const juce::Identifier kInput      { "Input" };
    const juce::Identifier kGate       { "Gate" };
    const juce::Identifier kTranspose  { "Transpose" };
    const juce::Identifier kNAM        { "NAM" };
    const juce::Identifier kNAMSlot    { "Slot" };
    const juce::Identifier kIR         { "IR" };
    const juce::Identifier kEQ         { "EQ" };
    const juce::Identifier kAutoLevel  { "AutoLevel" };
    const juce::Identifier kDoubler    { "Doubler" };
    const juce::Identifier kOutput     { "Output" };
    const juce::Identifier kPreFx      { "PreFxChainXml" };
    const juce::Identifier kPostFx     { "PostFxChainXml" };
    const juce::Identifier kCategory   { "category" };
    const juce::Identifier kTags       { "tags" };
    // Embedded scene bank (so each preset carries its own 4 scenes).
    const juce::Identifier kSceneBank  { "SceneBank" };
    const juce::Identifier kActiveScene{ "activeScene" };

    // ---- Preset schema version --------------------------------------------
    // v1: Original schema.
    // v2: Added per-NAM-slot "file" attribute, <NAM><Hosted> child for
    //     amp-sim plugins, and gate "attackMs"/"releaseMs"/"holdMs".
    //     All additions are backward-compatible reads (defaults applied
    //     when keys are missing), so v1 presets load cleanly.
    constexpr int kCurrentVersion = 2;

    /** Performs stepwise migrations on `v` so that, on return, it matches
     *  the current schema (kCurrentVersion). Returns the original on-disk
     *  version for logging purposes. Safe to call repeatedly: each step
     *  is idempotent and gated on the version it upgrades from. */
    int migratePresetInPlace (juce::ValueTree& v)
    {
        const int fileVersion = (int) v.getProperty (kVersion, 1);
        int ver = fileVersion;

        // v1 -> v2: purely additive (new optional keys). No structural
        // rewrites needed; defaults in restoreState() cover missing keys.
        // We still bump the marker so subsequent save-outs are tagged v2.
        if (ver < 2) ver = 2;

        if (ver != fileVersion)
            v.setProperty (kVersion, ver, nullptr);

        return fileVersion;
    }
}

PresetManager::PresetManager (AudioEngine& eng, PluginManager& mgr)
    : engine (eng), plugins (mgr) {}

juce::File PresetManager::presetsDirectory() const
{
    auto dir = ns::FileUtils::presetsDir();
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> PresetManager::listPresets() const
{
    juce::Array<juce::File> out;
    presetsDirectory().findChildFiles (out, juce::File::findFiles, false, "*.nspreset");
    return out;
}

juce::ValueTree PresetManager::captureState() const
{
    juce::ValueTree v (kRoot);
    v.setProperty (kVersion, kCurrentVersion, nullptr);
    if (activeCategory.isNotEmpty()) v.setProperty (kCategory, activeCategory, nullptr);
    if (activeTags    .isNotEmpty()) v.setProperty (kTags,     activeTags,     nullptr);

    {
        juce::ValueTree t (kInput);
        t.setProperty ("preGainDb", engine.getInput().getPreGainDb(), nullptr);
        v.appendChild (t, nullptr);
    }
    {
        juce::ValueTree t (kGate);
        t.setProperty ("thresholdDb", engine.getGate().getThresholdDb(), nullptr);
        t.setProperty ("attackMs",    engine.getGate().getAttackMs(),    nullptr);
        t.setProperty ("releaseMs",   engine.getGate().getReleaseMs(),   nullptr);
        t.setProperty ("holdMs",      engine.getGate().getHoldMs(),      nullptr);
        t.setProperty ("enabled",     engine.getGate().isEnabled(),     nullptr);
        v.appendChild (t, nullptr);
    }
    {
        juce::ValueTree t (kTranspose);
        t.setProperty ("semitones", engine.getTranspose().getSemitones(), nullptr);
        v.appendChild (t, nullptr);
    }
    {
        juce::ValueTree t (kNAM);
        for (int i = 0; i < 4; ++i)
        {
            juce::ValueTree s (kNAMSlot);
            s.setProperty ("index",  i, nullptr);
            s.setProperty ("weight", engine.getNAM().getSlotWeight (i), nullptr);
            // Per-slot on/off (blend-pad enable toggle). Without this a scene
            // recall left every loader in whatever on/off state the previous
            // scene had, even though the blend weights changed.
            s.setProperty ("bypassed", engine.getNAM().isSlotBypassed (i), nullptr);
            s.setProperty ("name",   engine.getNAM().getSlotName (i),   nullptr);
            // Persist the on-disk .nam file path so a preset / scene
            // recall reloads the correct model. Without this, presets
            // only restored the blend weights -- the same four amps
            // last loaded at the session would respond, which is wrong.
            const auto f = engine.getNAM().getSlotFile (i);
            if (f != juce::File())
                s.setProperty ("file", f.getFullPathName(), nullptr);
            t.appendChild (s, nullptr);
        }
        // Hosted amp-sim plugin (Neural DSP / ML Sound Lab / etc.) --
        // mutually exclusive with the NAM models from a routing POV.
        // Store the plugin's PluginDescription (so we can re-instantiate
        // on restore) plus the base64 of getStateInformation (so knob
        // positions, presets, IR selection survive).
        if (engine.getNAM().hasHostedPlugin())
        {
            juce::ValueTree h ("Hosted");
            const auto desc = engine.getNAM().getHostedPluginDescription();
            if (auto descXml = desc.createXml())
                h.setProperty ("descXml",
                               descXml->toString (juce::XmlElement::TextFormat().singleLine()),
                               nullptr);
            h.setProperty ("displayName",
                           engine.getNAM().getHostedPluginName(), nullptr);
            if (auto* inst = engine.getNAM().getHostedPluginInstance())
            {
                juce::MemoryBlock mb;
                inst->getStateInformation (mb);
                if (mb.getSize() > 0)
                    h.setProperty ("state", mb.toBase64Encoding(), nullptr);
            }
            t.appendChild (h, nullptr);
        }
        // Blend-pad dot position (x,y). The per-slot weights are derived from
        // this, but the UI puck reads the (x,y) directly -- capturing it is what
        // lets a scene recall move the dot to the stored position instead of
        // leaving it where the previous scene left it.
        t.setProperty ("xyBlendX", engine.getNAM().getXYBlendX(), nullptr);
        t.setProperty ("xyBlendY", engine.getNAM().getXYBlendY(), nullptr);
        // Oversampling mode (Off/Auto/2x/4x).
        t.setProperty ("osMode", (int) engine.getNAM().getOversamplingMode(), nullptr);
        v.appendChild (t, nullptr);
    }
    // (Built-in IR convolver removed — IR loading is now a plugin in the
    // post-FX chain. Old presets with an <IR> node are silently ignored.)
    {
        juce::ValueTree t (kEQ);
        t.setProperty ("bass",   engine.getEQ().getBass(),   nullptr);
        t.setProperty ("mid",    engine.getEQ().getMid(),    nullptr);
        t.setProperty ("treble", engine.getEQ().getTreble(), nullptr);
        v.appendChild (t, nullptr);
    }
    {
        juce::ValueTree t (kAutoLevel);
        t.setProperty ("macro", engine.getAutoLevelMacro(), nullptr);
        t.setProperty ("on",    engine.isAutoLevelOn(),     nullptr);
        v.appendChild (t, nullptr);
    }
    {
        juce::ValueTree t ("Tone");
        t.setProperty ("sweetSpot",   engine.getSweetSpot().sweetSpot.load(), nullptr);
        t.setProperty ("sweetSpotOn", ! engine.getSweetSpot().bypassed.load(), nullptr);
        t.setProperty ("tight",     engine.getTight(), nullptr);
        t.setProperty ("body",      engine.getBody(),  nullptr);
        t.setProperty ("air",       engine.getAir(),   nullptr);
        v.appendChild (t, nullptr);
    }
    {
        juce::ValueTree t (kDoubler);
        t.setProperty ("width", engine.getDoublerWidth(), nullptr);
        t.setProperty ("mix",   engine.getDoublerMix(),   nullptr);
        v.appendChild (t, nullptr);
    }
    {
        juce::ValueTree t (kOutput);
        t.setProperty ("postGainDb", engine.getOutput().getPostGainDb(), nullptr);
        t.setProperty ("muted",      engine.getOutput().isMuted(),       nullptr);
        t.setProperty ("limiterOn",  engine.getOutput().isSafetyLimiterEnabled(), nullptr);
        t.setProperty ("limiterCeilDb", engine.getOutput().getSafetyCeilingDb(),  nullptr);
        // A/B loudness-match trim: persisted so loading a preset after
        // a Match Levels pass keeps the measured dB delta instead of
        // snapping back to 0 and changing the perceived loudness.
        t.setProperty ("abTrimDb",   engine.getOutput().getAbTrimDb(),    nullptr);
        v.appendChild (t, nullptr);
    }

    // Chain serialisation -- the SLOW part. saveStateToXml() walks every
    // hosted plugin and base64-encodes their state blob, which can take
    // tens of ms per chain (NDSP / IR loaders / reverbs all carry big
    // blobs). On a scene click the host calls pushUndoSnapshot() which
    // ends up here, BEFORE the recall starts -- so we skip the walk and
    // reuse the cached XML string when the chain has not been structurally
    // mutated (publish() bumps mutationGen) since we last cached it. The
    // cache is seeded by restoreChain() too, so a scene recall + immediate
    // re-capture both hit the fast path.
    auto captureChain = [] (PluginChain& chain, juce::String& cachedXml, int& cachedGen) -> juce::String
    {
        const int gen = chain.getMutationGen();
        if (cachedGen == gen && cachedXml.isNotEmpty())
            return cachedXml;
        if (auto xml = chain.saveStateToXml())
        {
            cachedXml = xml->toString (juce::XmlElement::TextFormat().singleLine());
            cachedGen = gen;
            return cachedXml;
        }
        return {};
    };
    auto preStr  = captureChain (engine.getPreFxChain (), lastPreFxXml,  lastPreFxGen);
    auto postStr = captureChain (engine.getPostFxChain(), lastPostFxXml, lastPostFxGen);
    if (preStr .isNotEmpty()) v.setProperty (kPreFx,  preStr,  nullptr);
    if (postStr.isNotEmpty()) v.setProperty (kPostFx, postStr, nullptr);

    return v;
}

void PresetManager::restoreState (const juce::ValueTree& state)
{
    if (! state.hasType (kRoot)) return;

    // Migrate older preset schemas in-place so the rest of this function
    // can assume the current layout. We work on a local copy to keep the
    // caller's tree untouched.
    juce::ValueTree migrated = state.createCopy();
    const int fileVersion = migratePresetInPlace (migrated);
    if (fileVersion < kCurrentVersion)
        DBG ("PresetManager: migrated preset from v" << fileVersion
             << " to v" << kCurrentVersion);

    const juce::ValueTree& v = migrated;

    if (auto t = v.getChildWithName (kInput); t.isValid())
        engine.getInput().setPreGainDb ((float) t.getProperty ("preGainDb", 0.0));

    if (auto t = v.getChildWithName (kGate); t.isValid())
    {
        engine.getGate().setThresholdDb ((float) t.getProperty ("thresholdDb", -60.0));
        engine.getGate().setAttackMs    ((float) t.getProperty ("attackMs",      3.0));
        engine.getGate().setReleaseMs   ((float) t.getProperty ("releaseMs",   120.0));
        engine.getGate().setHoldMs      ((float) t.getProperty ("holdMs",       20.0));
        engine.getGate().setEnabled     ((bool)  t.getProperty ("enabled", false));
    }

    if (auto t = v.getChildWithName (kTranspose); t.isValid())
        engine.getTranspose().setSemitones ((float) t.getProperty ("semitones", 0.0));

    if (auto t = v.getChildWithName (kNAM); t.isValid())
    {
        for (auto s : t)
        {
            if (! s.hasType (kNAMSlot)) continue;
            const int i = (int) s.getProperty ("index", -1);
            if (! juce::isPositiveAndBelow (i, 4)) continue;

            engine.getNAM().setSlotWeight (i, (float) s.getProperty ("weight", 0.0));
            // Per-slot on/off (blend-pad enable). Restored so a scene recall
            // brings back the exact loader on/off layout it was captured with.
            engine.getNAM().setSlotBypassed (i, (bool) s.getProperty ("bypassed", false));

            // Re-load the .nam file the preset / scene captured. Skip
            // if the same file is already loaded so back-to-back scene
            // recalls of identical NAM configs don't re-prime the model
            // (which would briefly mute the slot).
            const auto path = s.getProperty ("file", juce::String()).toString();
            if (path.isNotEmpty())
            {
                const juce::File modelFile (path);
                if (modelFile.existsAsFile()
                    && engine.getNAM().getSlotFile (i).getFullPathName() != path)
                {
                    juce::String err;
                    engine.getNAM().loadSlot (i, modelFile, err);
                }
            }
            else
            {
                // Preset captured an empty slot -- clear any model currently
                // loaded there so the recall is exact.
                if (engine.getNAM().hasSlot (i))
                    engine.getNAM().clearSlot (i);
            }
        }

        // Oversampling mode.
        if (t.hasProperty ("osMode"))
            engine.getNAM().setOversamplingMode (
                (NAMProcessor::OsMode) (int) t.getProperty ("osMode", 0));

        // Blend-pad dot position. Applied AFTER the slots are (re)loaded above
        // so setXYBlend() can recompute weights over the loaded slots, and the
        // UI puck (which reads the live x,y) snaps to the captured position
        // instead of staying where the previous scene left it.
        if (t.hasProperty ("xyBlendX") && t.hasProperty ("xyBlendY"))
            engine.getNAM().setXYBlend ((float) t.getProperty ("xyBlendX", 0.5),
                                        (float) t.getProperty ("xyBlendY", 0.5));

        // Hosted amp-sim plugin. Three cases:
        //  1. Preset has <Hosted> AND same plugin already hosted -> just
        //     push state (cheap, no re-instantiation).
        //  2. Preset has <Hosted> with different/no current plugin -> async
        //     instantiate via the format manager, install, then push state.
        //  3. Preset has NO <Hosted> but one is currently hosted -> clear it.
        if (auto h = t.getChildWithName ("Hosted"); h.isValid())
        {
            const auto descXmlText = h.getProperty ("descXml", juce::String()).toString();
            const auto displayName = h.getProperty ("displayName",
                                                    juce::String ("Hosted plugin")).toString();
            const auto stateB64    = h.getProperty ("state", juce::String()).toString();

            juce::PluginDescription desc;
            if (descXmlText.isNotEmpty())
                if (auto xml = juce::parseXML (descXmlText))
                    desc.loadFromXml (*xml);

            auto applyState = [stateB64] (juce::AudioPluginInstance* inst)
            {
                if (inst == nullptr || stateB64.isEmpty()) return;
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (stateB64))
                    inst->setStateInformation (mb.getData(), (int) mb.getSize());
            };

            const auto& curDesc = engine.getNAM().getHostedPluginDescription();
            const bool sameId = desc.fileOrIdentifier.isNotEmpty()
                                && desc.fileOrIdentifier == curDesc.fileOrIdentifier;

            if (sameId)
            {
                applyState (engine.getNAM().getHostedPluginInstance());
            }
            else if (desc.fileOrIdentifier.isNotEmpty())
            {
                const double sr = engine.getCurrentSampleRate();
                const int    bs = engine.getCurrentBlockSize();
                plugins.beginGuardedAdd (desc.fileOrIdentifier);
                plugins.getFormats().createPluginInstanceAsync (desc, sr, bs,
                    [displayName, applyState] (std::unique_ptr<juce::AudioPluginInstance> inst,
                                               const juce::String&)
                    {
                        App::get().getPluginManager().endGuardedAdd();
                        if (inst == nullptr) return;
                        applyState (inst.get());
                        App::get().getAudioEngine().getNAM().setHostedPlugin (std::move (inst),
                                                                              displayName);
                    });
            }
        }
        else if (engine.getNAM().hasHostedPlugin())
        {
            engine.getNAM().clearHostedPlugin();
        }
    }

    // <IR> node from legacy presets is intentionally ignored — IRs are now
    // restored as part of the post-FX plugin-chain state below.

    if (auto t = v.getChildWithName (kEQ); t.isValid())
    {
        engine.getEQ().setBass   ((float) t.getProperty ("bass",   0.0));
        engine.getEQ().setMid    ((float) t.getProperty ("mid",    0.0));
        engine.getEQ().setTreble ((float) t.getProperty ("treble", 0.0));
    }

    if (auto t = v.getChildWithName (kAutoLevel); t.isValid())
    {
        engine.setAutoLevelMacro ((float) t.getProperty ("macro", 0.5));
        engine.setAutoLevelOn    ((bool)  t.getProperty ("on",    false));
    }

    if (auto t = v.getChildWithName (juce::Identifier ("Tone")); t.isValid())
    {
        engine.getSweetSpot().sweetSpot.store ((float) t.getProperty ("sweetSpot", 0.5));
        engine.getSweetSpot().bypassed .store (! (bool) t.getProperty ("sweetSpotOn", false));
        engine.setTight ((float) t.getProperty ("tight", 0.0));
        engine.setBody  ((float) t.getProperty ("body",  0.5));
        engine.setAir   ((float) t.getProperty ("air",   0.5));
    }

    if (auto t = v.getChildWithName (kDoubler); t.isValid())
    {
        engine.setDoublerWidth ((float) t.getProperty ("width", 0.0));
        engine.setDoublerMix   ((float) t.getProperty ("mix",   0.0));
    }

    if (auto t = v.getChildWithName (kOutput); t.isValid())
    {
        engine.getOutput().setPostGainDb ((float) t.getProperty ("postGainDb", 0.0));
        engine.getOutput().setMute       ((bool)  t.getProperty ("muted",      false));
        engine.getOutput().setSafetyLimiterEnabled ((bool)  t.getProperty ("limiterOn",     true));
        engine.getOutput().setSafetyCeilingDb     ((float) t.getProperty ("limiterCeilDb", -0.3));
        engine.getOutput().setAbTrimDb            ((float) t.getProperty ("abTrimDb",      0.0));
    }

    auto restoreChain = [&] (PluginChain& chain, const juce::String& xmlText,
                             juce::String& cachedXml, int& cachedGen)
    {
        if (xmlText.isEmpty()) return;
        // Fast path: identical to the last successfully-applied XML, AND no
        // structural mutation has happened on the live chain since then.
        // Avoids parseXML + restoreStateFromXml's per-slot work entirely
        // when the user re-loads the same scene back-to-back.
        const int liveGen = chain.getMutationGen();
        if (cachedGen == liveGen && cachedXml == xmlText)
            return;
        // Slow path: parse + delegate. PluginChain::restoreStateFromXml has
        // its OWN in-place fast path (compares composition by plugin ID,
        // hashes each plugin's state to skip unchanged setStateInformation
        // calls), which avoids the audible silence-gap on scene switching.
        // We deliberately do NOT call chain.saveStateToXml() here for an
        // up-front equality check -- serialising every hosted plugin's
        // state on every scene recall is itself the dozens-of-ms cost we
        // are trying to eliminate. Trust the in-place path inside
        // restoreStateFromXml() instead.
        if (auto x = juce::parseXML (xmlText))
        {
            chain.restoreStateFromXml (*x, plugins.getFormats(), plugins.getKnownList());
            cachedXml = xmlText;
            cachedGen = chain.getMutationGen(); // bumped only if a real teardown happened
        }
    };
    restoreChain (engine.getPreFxChain(),  v.getProperty (kPreFx,  juce::String()).toString(),
                  lastPreFxXml,  lastPreFxGen);
    restoreChain (engine.getPostFxChain(), v.getProperty (kPostFx, juce::String()).toString(),
                  lastPostFxXml, lastPostFxGen);
}

bool PresetManager::save (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    // File save is the user's explicit "commit" -- bypass the chain XML
    // cache so any internal-GUI parameter tweaks that didn't bump
    // mutationGen are captured byte-accurately to disk.
    const_cast<PresetManager*> (this)->invalidateChainCache();
    auto state = captureState();

    // Embed the full 4-scene bank so each preset is a SELF-CONTAINED set of
    // scenes. Previously scenes were global and shared across every preset,
    // so switching presets never changed the scenes' sounds -- only the SCENE
    // button labels appeared to change. Now the bank (each scene's NAM models,
    // FX chains, blend dot, on/off, names, trims) travels inside the preset,
    // plus the index of the scene that was active when saved.
    state.appendChild (App::get().getSceneManager().toValueTree(), nullptr);
    state.setProperty (kActiveScene, App::get().readLastActiveScene(), nullptr);

    if (auto xml = state.createXml())
    {
        currentPresetFile = file;
        return xml->writeTo (file);
    }
    return false;
}

bool PresetManager::load (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    if (auto xml = juce::parseXML (file))
    {
        auto v = juce::ValueTree::fromXml (*xml);
        if (v.isValid())
        {
            auto& sm = App::get().getSceneManager();
            auto bank = v.getChildWithName (kSceneBank);
            int activeIdx = -1;

            if (bank.isValid())
            {
                // New-style preset: the embedded scene bank drives the sound.
                // Restore all four scenes, then recall the one that was active
                // when the preset was saved (recall() applies that scene's full
                // state via restoreState + a click-free duck). We do NOT also
                // run the top-level restoreState here -- that would reload the
                // NAM models / chains twice and could glitch.
                sm.fromValueTree (bank);

                activeIdx = (int) v.getProperty (kActiveScene, 0);
                if (! juce::isPositiveAndBelow (activeIdx, SceneManager::kNumScenes)
                    || ! sm.hasScene (activeIdx))
                {
                    // Stored active scene is empty -- fall back to the first
                    // non-empty scene so the preset still makes sound.
                    activeIdx = -1;
                    for (int i = 0; i < SceneManager::kNumScenes; ++i)
                        if (sm.hasScene (i)) { activeIdx = i; break; }
                }

                if (activeIdx >= 0)
                    sm.recall (activeIdx);
                else
                    restoreState (v); // every scene empty -> use top-level state
            }
            else
            {
                // Legacy single-state preset (no embedded scene bank).
                restoreState (v);
            }

            currentPresetFile = file;
            // Cache metadata so a subsequent re-save keeps the same tags/category.
            activeCategory = v.getProperty (kCategory, juce::String()).toString();
            activeTags     = v.getProperty (kTags,     juce::String()).toString();

            App::get().persistLastActiveScene (activeIdx);
            if (sm.onBankReloaded) sm.onBankReloaded(); // refresh SCENE labels/LED
            return true;
        }
    }
    return false;
}

juce::String PresetManager::readCategoryFromFile (const juce::File& f)
{
    if (! f.existsAsFile()) return {};
    if (auto xml = juce::parseXML (f))
        return xml->getStringAttribute ("category");
    return {};
}

juce::String PresetManager::readTagsFromFile (const juce::File& f)
{
    if (! f.existsAsFile()) return {};
    if (auto xml = juce::parseXML (f))
        return xml->getStringAttribute ("tags");
    return {};
}

bool PresetManager::writeMetaToFile (const juce::File& f,
                                     const juce::String& category,
                                     const juce::String& tags)
{
    if (! f.existsAsFile()) return false;
    auto xml = juce::parseXML (f);
    if (xml == nullptr) return false;
    // setAttribute with an empty string removes the attribute, which is what
    // we want for "Uncategorised" / no tags.
    if (category.trim().isEmpty()) xml->removeAttribute ("category");
    else                           xml->setAttribute   ("category", category.trim());
    if (tags.trim().isEmpty())     xml->removeAttribute ("tags");
    else                           xml->setAttribute   ("tags",     tags.trim());
    return xml->writeTo (f);
}
