#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class SceneBar : public juce::Component,
                 private juce::Timer
{
public:
    SceneBar();
    ~SceneBar() override;

    void resized() override;

    /** Sets a custom label for a scene button (e.g. preset/scene name). */
    void setSceneName (int idx, const juce::String& name);

    /** Mark a scene as "dirty" (its current state differs from its
     *  captured snapshot). Painted as a small dot on the button. */
    void setSceneModified (int idx, bool modified);

    /** Per-scene loudness-trim badge (e.g. "+1.5 dB"). 0 hides it. */
    void setSceneTrimDb (int idx, float db);

    std::function<void (int)> onSceneSelected;     // left-click  -> recall
    std::function<void (int)> onSceneRightClick;   // right-click -> menu

    /** Visually mark a scene as active (lit) WITHOUT triggering recall.
     *  Used at boot to restore the last-active scene indicator after the
     *  audio state was already loaded from the autosaved chain, AND by
     *  the SceneManager::onRecalled hook so MIDI-PC / learned-CC recalls
     *  also light the correct button. We deliberately do NOT fire the
     *  onSceneSelected callback here -- otherwise we'd re-enter recall
     *  on the same scene from inside the recall finish hook.  */
    void setActiveScene (int idx);
    int  getActiveScene() const noexcept { return activeScene; }

private:
    class SceneButton;
    std::array<std::unique_ptr<SceneButton>, 4> buttons;
    int activeScene { 0 };

    void setActive (int idx);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneBar)
};
