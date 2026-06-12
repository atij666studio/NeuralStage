#include "SceneBar.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../Styles/AppLNF.h"
#include "../../App.h"
#include "../../Core/SceneManager.h"

namespace
{
    class SceneButtonLNF : public juce::LookAndFeel_V4
    {
    public:
        void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                   const juce::Colour& /*bg*/,
                                   bool isOver, bool isDown) override
        {
            const bool sel = b.getToggleState();
            auto r = b.getLocalBounds().toFloat().reduced (1.0f);

            juce::Colour col = sel ? ns::Colours::tealAccent
                                   : ns::Colours::chipUnsel;
            if (isDown) col = col.brighter (0.10f);
            else if (isOver) col = col.brighter (0.05f);

            g.setColour (col);
            g.fillRoundedRectangle (r, (float) ns::UI::kSceneRadius);

            if (sel)
            {
                g.setColour (ns::Colours::tealLight.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (1.0f),
                                        (float) ns::UI::kSceneRadius - 1.0f, 1.5f);
            }
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                             bool /*isHi*/, bool /*isDown*/) override
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle (
                          b.getToggleState() ? "Bold" : "")));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds(),
                              juce::Justification::centred, 1);
        }
    };

    inline SceneButtonLNF& sceneLNF() { static SceneButtonLNF inst; return inst; }
}

class SceneBar::SceneButton : public juce::TextButton
{
public:
    bool  modified { false };
    float trimDb   { 0.0f };
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
        {
            if (onRightClick) onRightClick();
            return;
        }
        juce::TextButton::mouseDown (e);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (modified)
        {
            // Small amber dot in the top-right corner = "changed since recall".
            const float d = juce::jlimit (5.0f, 8.0f, (float) getHeight() * 0.18f);
            juce::Rectangle<float> dot ((float) getWidth() - d - 5.0f, 4.0f, d, d);
            g.setColour (juce::Colour (0xffe6b400));
            g.fillEllipse (dot);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawEllipse (dot, 0.8f);
        }
        if (std::abs (trimDb) >= 0.05f)
        {
            // Bottom-left small badge with sign & value, e.g. "+1.5 dB".
            juce::String txt = (trimDb > 0.0f ? "+" : "")
                             + juce::String (trimDb, 1) + " dB";
            g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            juce::Rectangle<int> r (4, getHeight() - 14, getWidth() - 8, 12);
            g.drawFittedText (txt, r, juce::Justification::bottomLeft, 1);
        }
    }
};

SceneBar::SceneBar()
{
    for (int i = 0; i < (int) buttons.size(); ++i)
    {
        buttons[(size_t) i] = std::make_unique<SceneButton>();
        auto& b = *buttons[(size_t) i];
        b.setButtonText ("SCENE " + juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1);
        b.setLookAndFeel (&sceneLNF());
        b.onClick = [this, i] { setActive (i); };
        b.onRightClick = [this, i] { if (onSceneRightClick) onSceneRightClick (i); };
        b.setTooltip ("Click to recall -- right-click to capture / rename / clear.");
        addAndMakeVisible (b);
    }
    setActive (0);
    startTimerHz (3); // light-touch poll for scene-modified state.
}

SceneBar::~SceneBar()
{
    for (auto& b : buttons) if (b) b->setLookAndFeel (nullptr);
}

void SceneBar::setSceneName (int idx, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (idx, (int) buttons.size())) return;
    buttons[(size_t) idx]->setButtonText (name);
    buttons[(size_t) idx]->repaint();
}

void SceneBar::setSceneModified (int idx, bool modified)
{
    if (! juce::isPositiveAndBelow (idx, (int) buttons.size())) return;
    auto& b = *buttons[(size_t) idx];
    if (b.modified != modified) { b.modified = modified; b.repaint(); }
}

void SceneBar::setSceneTrimDb (int idx, float db)
{
    if (! juce::isPositiveAndBelow (idx, (int) buttons.size())) return;
    auto& b = *buttons[(size_t) idx];
    if (std::abs (b.trimDb - db) > 0.01f) { b.trimDb = db; b.repaint(); }
}

void SceneBar::timerCallback()
{
    // Only check the active scene -- recall always restores it, so the
    // others are by definition "unchanged since their own capture".
    auto& sm = App::get().getSceneManager();

    // Keep the button labels in sync with the SceneManager (the source of
    // truth). Names are restored at boot from the persisted app state and can
    // change via rename / clear / preset load, none of which push directly to
    // this bar -- so we reconcile here every poll. Cheap: a string compare per
    // button, only repainting when the text actually differs.
    for (int i = 0; i < (int) buttons.size(); ++i)
    {
        const auto nm = sm.getName (i);
        if (buttons[(size_t) i]->getButtonText() != nm)
            setSceneName (i, nm);
    }

    const bool dirty = sm.hasScene (activeScene)
                       && ! sm.currentMatches (activeScene);
    setSceneModified (activeScene, dirty);
    // Clear stale dots on the others.
    for (int i = 0; i < (int) buttons.size(); ++i)
        if (i != activeScene) setSceneModified (i, false);

    // Trim badges always reflect the stored value (only shown if non-zero).
    for (int i = 0; i < (int) buttons.size(); ++i)
        setSceneTrimDb (i, sm.hasScene (i) ? sm.getTrimDb (i) : 0.0f);
}

void SceneBar::setActive (int idx)
{
    activeScene = idx;
    for (int i = 0; i < (int) buttons.size(); ++i)
        buttons[(size_t) i]->setToggleState (i == idx, juce::dontSendNotification);
    if (onSceneSelected) onSceneSelected (idx);
}

void SceneBar::setActiveScene (int idx)
{
    // Visual-only: light the LED + clear dirty dots on the others, but do
    // NOT fire onSceneSelected (that would re-enter recall on the same
    // scene from inside the recall-finished hook).
    if (! juce::isPositiveAndBelow (idx, (int) buttons.size())) return;
    activeScene = idx;
    for (int i = 0; i < (int) buttons.size(); ++i)
        buttons[(size_t) i]->setToggleState (i == idx, juce::dontSendNotification);
}

void SceneBar::resized()
{
    using namespace ns::UI;
    const int n = (int) buttons.size();
    const int totalW = n * kSceneBtnW + (n - 1) * kSceneSpacing;
    int x = (getWidth() - totalW) / 2;
    // Button height scales proportionally with the panel (which MainComponent
    // shrinks at small screen heights).  The formula matches what MainComponent
    // computes for effectiveSceneBtnH: max(28, getHeight() - 40).
    const int btnH = juce::jmin (kSceneBtnH, juce::jmax (28, getHeight() - 40));
    const int y = (getHeight() - btnH) / 2;

    for (int i = 0; i < n; ++i)
    {
        buttons[(size_t) i]->setBounds (x, y, kSceneBtnW, btnH);
        x += kSceneBtnW + kSceneSpacing;
    }
}
