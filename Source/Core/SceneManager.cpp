#include "SceneManager.h"
#include "PresetManager.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/NAM/NAMProcessor.h"

namespace
{
    const juce::Identifier kBank   { "SceneBank" };
    const juce::Identifier kScene  { "Scene" };
    const juce::Identifier kIndex  { "index" };
    const juce::Identifier kName   { "name" };
    const juce::Identifier kTrimDb { "trimDb" };

    // Default scene-switch crossfade. A short non-zero window makes live
    // scene changes click-free yet still perceptually instant: every morphed
    // scalar (gains, EQ, tone, doubler, NAM weights, scene trim) glides over
    // ~40 ms instead of hard-jumping. The user can override this from the
    // MORPH button (0 = hard/instant, up to 500 ms for long crossfades).
    std::atomic<int> gMorphMs { 40 };

    // ----- Continuous-parameter morph plumbing -----
    // We morph these scalar engine parameters smoothly across the recall
    // window. Plugin-chain restore runs once up-front (XML-diff skip in
    // PresetManager keeps it cheap if unchanged).
    struct MorphParams
    {
        float input = 0, postGain = 0, sceneTrim = 0;
        float bass = 0, mid = 0, treble = 0;
        float autoLevel = 0.5f;
        float doublerWidth = 0, doublerMix = 0;
        float transpose = 0;
        float namPre = 0, namPost = 0;
        float wA = 0, wB = 0, wC = 0, wD = 0;
        float sweetSpot = 0.5f;
        float tight = 0, body = 0.5f, air = 0.5f;
    };

    static MorphParams readEngine (AudioEngine& e, float trimDb)
    {
        MorphParams p;
        p.input        = e.getInput().getPreGainDb();
        p.postGain     = e.getOutput().getPostGainDb();
        p.sceneTrim    = trimDb;
        p.bass         = e.getEQ().getBass();
        p.mid          = e.getEQ().getMid();
        p.treble       = e.getEQ().getTreble();
        p.autoLevel    = e.getAutoLevelMacro();
        p.doublerWidth = e.getDoublerWidth();
        p.doublerMix   = e.getDoublerMix();
        p.transpose    = e.getTranspose().getSemitones();
        p.namPre       = e.getNAM().getPreGain();
        p.namPost      = e.getNAM().getPostGain();
        p.wA           = e.getNAM().getSlotWeight (0);
        p.wB           = e.getNAM().getSlotWeight (1);
        p.wC           = e.getNAM().getSlotWeight (2);
        p.wD           = e.getNAM().getSlotWeight (3);
        p.sweetSpot    = e.getSweetSpot().sweetSpot.load();
        p.tight        = e.getTight();
        p.body         = e.getBody();
        p.air          = e.getAir();
        return p;
    }

    static void writeEngine (AudioEngine& e, const MorphParams& p)
    {
        e.getInput()    .setPreGainDb (p.input);
        e.getOutput()   .setPostGainDb (p.postGain);
        e.getOutput()   .setSceneTrimDb (p.sceneTrim);
        e.getEQ()       .setBass   (p.bass);
        e.getEQ()       .setMid    (p.mid);
        e.getEQ()       .setTreble (p.treble);
        e.setAutoLevelMacro (p.autoLevel);
        e.setDoublerWidth   (p.doublerWidth);
        e.setDoublerMix     (p.doublerMix);
        e.getTranspose().setSemitones (p.transpose);
        e.getNAM()      .setPreGain  (p.namPre);
        e.getNAM()      .setPostGain (p.namPost);
        e.getNAM()      .setSlotWeight (0, p.wA);
        e.getNAM()      .setSlotWeight (1, p.wB);
        e.getNAM()      .setSlotWeight (2, p.wC);
        e.getNAM()      .setSlotWeight (3, p.wD);
        e.getSweetSpot().sweetSpot.store (p.sweetSpot);
        e.setTight (p.tight);
        e.setBody  (p.body);
        e.setAir   (p.air);
    }

    static MorphParams lerp (const MorphParams& a, const MorphParams& b, float t)
    {
        auto L = [t] (float x, float y) { return x + (y - x) * t; };
        MorphParams r;
        r.input=L(a.input,b.input); r.postGain=L(a.postGain,b.postGain); r.sceneTrim=L(a.sceneTrim,b.sceneTrim);
        r.bass=L(a.bass,b.bass); r.mid=L(a.mid,b.mid); r.treble=L(a.treble,b.treble);
        r.autoLevel=L(a.autoLevel,b.autoLevel);
        r.doublerWidth=L(a.doublerWidth,b.doublerWidth); r.doublerMix=L(a.doublerMix,b.doublerMix);
        r.transpose=L(a.transpose,b.transpose);
        r.namPre=L(a.namPre,b.namPre); r.namPost=L(a.namPost,b.namPost);
        r.wA=L(a.wA,b.wA); r.wB=L(a.wB,b.wB); r.wC=L(a.wC,b.wC); r.wD=L(a.wD,b.wD);
        r.sweetSpot=L(a.sweetSpot,b.sweetSpot);
        r.tight=L(a.tight,b.tight); r.body=L(a.body,b.body); r.air=L(a.air,b.air);
        return r;
    }

    // Generation counter -- if recall() is called again mid-morph, the older
    // timer notices the bump and self-destructs without touching the engine.
    std::atomic<int> gMorphGen { 0 };

    class MorphTimer : public juce::Timer
    {
    public:
        MorphTimer (AudioEngine& e, MorphParams f, MorphParams t, int durMs, int gen)
            : eng (e), from (f), to (t), totalMs (durMs), myGen (gen)
        {
            startTime = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (60);
        }
        void timerCallback() override
        {
            if (gMorphGen.load() != myGen)
            {
                stopTimer();
                delete this;
                return;
            }
            const double now = juce::Time::getMillisecondCounterHiRes();
            float t = (float) ((now - startTime) / (double) juce::jmax (1, totalMs));
            if (t >= 1.0f)
            {
                writeEngine (eng, to);
                stopTimer();
                delete this;
                return;
            }
            const float te = t * t * (3.0f - 2.0f * t); // smoothstep
            writeEngine (eng, lerp (from, to, te));
        }
    private:
        AudioEngine& eng;
        MorphParams  from, to;
        int          totalMs;
        int          myGen;
        double       startTime;
    };
}

int  SceneManager::getMorphMs() noexcept       { return gMorphMs.load(); }
void SceneManager::setMorphMs (int ms) noexcept { gMorphMs.store (juce::jlimit (0, 500, ms)); }

SceneManager::SceneManager (PresetManager& pm) : presets (pm) {}

void SceneManager::capture (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumScenes)) return;
    // Capturing a scene is an explicit user "commit" -- bypass the chain-XML
    // cache first. The cache is only kept fresh on structural chain mutations
    // (add/remove plugin); parameter tweaks made INSIDE a hosted plugin's own
    // GUI (IR selection, amp-sim knobs, reverb settings) do NOT bump the
    // chain's mutationGen. Without this invalidation every capture re-uses the
    // first-captured chain XML, so all scenes end up byte-identical and scene
    // switching appears to "do nothing". File-save already does this for the
    // same reason.
    presets.invalidateChainCache();
    scenes[(size_t) i] = presets.captureState();
}

bool SceneManager::recall (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumScenes)) return false;
    if (! scenes[(size_t) i].isValid()) return false;

    const int morph = gMorphMs.load();
    auto& eng = presets.getEngine();
    auto& out = eng.getOutput();

    // Fade the output to silence across the swap so any chain / NAM-model
    // discontinuity (or the brief morph re-seed) is inaudible -- the scene
    // change is silent and click-free regardless of morph length.
    out.notifySceneSwitch();

    if (morph <= 0)
    {
        gMorphGen.fetch_add (1); // abort any in-flight morph
        presets.restoreState (scenes[(size_t) i]);
        out.setSceneTrimDb (trimDb[(size_t) i]);
        if (onRecalled) onRecalled (i);
        return true;
    }

    // Parameter-morph recall. Snapshot the "from" scalar values, swap the
    // plugin chain up-front (PresetManager XML-diff skips when unchanged --
    // no teardown), then re-seed engine to "from" and morph smoothly to
    // "to" across the window. Replaces the previous mute-swap-mute, which
    // produced an audible silence gap.
    const float fromTrim = out.getSceneTrimDb();
    MorphParams from = readEngine (eng, fromTrim);

    presets.restoreState (scenes[(size_t) i]);
    out.setSceneTrimDb (trimDb[(size_t) i]);
    MorphParams to = readEngine (eng, trimDb[(size_t) i]);

    writeEngine (eng, from); // re-seed scalar params back to "from"

    const int gen = gMorphGen.fetch_add (1) + 1;
    new MorphTimer (eng, from, to, morph, gen); // self-deleting
    if (onRecalled) onRecalled (i);
    return true;
}

bool SceneManager::hasScene (int i) const
{
    return juce::isPositiveAndBelow (i, kNumScenes) && scenes[(size_t) i].isValid();
}

juce::String SceneManager::getName (int i) const
{
    return juce::isPositiveAndBelow (i, kNumScenes) ? names[(size_t) i] : juce::String();
}

void SceneManager::setName (int i, const juce::String& n)
{
    if (juce::isPositiveAndBelow (i, kNumScenes)) names[(size_t) i] = n;
}

void SceneManager::clear (int i)
{
    if (juce::isPositiveAndBelow (i, kNumScenes))
    {
        scenes[(size_t) i] = {};
        trimDb[(size_t) i] = 0.0f;
    }
}

float SceneManager::getTrimDb (int i) const
{
    return juce::isPositiveAndBelow (i, kNumScenes) ? trimDb[(size_t) i] : 0.0f;
}

void SceneManager::setTrimDb (int i, float db)
{
    if (! juce::isPositiveAndBelow (i, kNumScenes)) return;
    trimDb[(size_t) i] = juce::jlimit (-24.0f, 24.0f, db);
    // If this scene is currently the live one, update audio immediately.
    if (currentMatches (i))
        presets.getEngine().getOutput().setSceneTrimDb (trimDb[(size_t) i]);
}

bool SceneManager::currentMatches (int i) const
{
    if (! juce::isPositiveAndBelow (i, kNumScenes)) return false;
    if (! scenes[(size_t) i].isValid())             return false;
    auto current = presets.captureState();
    return current.isEquivalentTo (scenes[(size_t) i]);
}

juce::ValueTree SceneManager::toValueTree() const
{
    juce::ValueTree v (kBank);
    for (int i = 0; i < kNumScenes; ++i)
    {
        juce::ValueTree s (kScene);
        s.setProperty (kIndex,  i,                 nullptr);
        s.setProperty (kName,   names[(size_t) i], nullptr);
        s.setProperty (kTrimDb, trimDb[(size_t) i], nullptr);
        if (scenes[(size_t) i].isValid())
            s.appendChild (scenes[(size_t) i].createCopy(), nullptr);
        v.appendChild (s, nullptr);
    }
    return v;
}

void SceneManager::fromValueTree (const juce::ValueTree& v)
{
    if (! v.hasType (kBank)) return;
    for (auto s : v)
    {
        const int i = (int) s.getProperty (kIndex, -1);
        if (! juce::isPositiveAndBelow (i, kNumScenes)) continue;
        names[(size_t) i] = s.getProperty (kName, "SCENE " + juce::String (i + 1)).toString();
        trimDb[(size_t) i] = (float) (double) s.getProperty (kTrimDb, 0.0);
        if (s.getNumChildren() > 0) scenes[(size_t) i] = s.getChild (0).createCopy();
        else                        scenes[(size_t) i] = {};
    }
}
