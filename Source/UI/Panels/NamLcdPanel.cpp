#include "NamLcdPanel.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../Styles/AppLNF.h"
#include "../Dialogs/ThemedAlerts.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"
#include "../../Utils/RecentFiles.h"

namespace
{
    /** TextButton LNF that paints letters stacked vertically, top-to-bottom
     *  (one glyph per row). Used for the C/D loaders so labels read naturally
     *  on tall narrow buttons. */
    class VerticalStackButtonLNF : public juce::LookAndFeel_V4
    {
    public:
        void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                             bool /*isHi*/, bool /*isDown*/) override
        {
            const auto txt = b.getButtonText();
            const auto r   = b.getLocalBounds().reduced (2, 6);

            const int n = txt.length();
            if (n <= 0) return;

            const float maxByH = (float) r.getHeight() / (float) juce::jmax (1, n);
            const float fontH  = juce::jlimit (8.0f, 16.0f, maxByH * 0.9f);

            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (fontH).withStyle ("Bold")));

            const float rowH = (float) r.getHeight() / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const auto cellY = r.getY() + (int) std::round (i * rowH);
                juce::Rectangle<int> cell (r.getX(), cellY,
                                           r.getWidth(),
                                           (int) std::ceil (rowH));
                g.drawFittedText (juce::String::charToString (txt[i]), cell,
                                  juce::Justification::centred, 1);
            }
        }
    };

    inline VerticalStackButtonLNF& vSlotLNF() { static VerticalStackButtonLNF inst; return inst; }
}

NamLcdPanel::NamLcdPanel()
{
    auto styleSlot = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,    ns::Colours::chipUnsel);
        b.setColour (juce::TextButton::buttonOnColourId,  ns::Colours::tealAccent);
        b.setColour (juce::TextButton::textColourOffId,   juce::Colours::white);
        b.setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
    };

    for (int i = 0; i < 4; ++i)
    {
        styleSlot (slotButtons[i]);
        // Left-click toggles bypass on/off for this slot.
        // Right-click (isPopupMenu) is handled in mouseDown via addMouseListener.
        slotButtons[i].onClick = [this, i]
        {
            auto& nam = App::get().getAudioEngine().getNAM();
            if (! nam.hasSlot (i)) return;
            App::get().pushUndoSnapshot();
            nam.setSlotBypassed (i, ! nam.isSlotBypassed (i));
            refreshSlotLabels();
        };
        slotButtons[i].addMouseListener (this, false);
        addAndMakeVisible (slotButtons[i]);
    }

    // C and D are vertical — paint their text rotated 90°.
    slotButtons[2].setLookAndFeel (&vSlotLNF());
    slotButtons[3].setLookAndFeel (&vSlotLNF());

    // Slim-size sliders: one per NAM slot; hidden until a SlimmableContainer model is loaded.
    // A/B are horizontal (fit in the 12px gap between button and blend pad).
    // C/D are vertical (fit in the 12px gap between the C/D button and the blend pad).
    for (int i = 0; i < 4; ++i)
    {
        auto& sl = slimSliders[i];
        // A (0) and B (1) are horizontal; C (2) and D (3) are vertical.
        const bool isVertical = (i == 2 || i == 3);
        sl.setSliderStyle (isVertical ? juce::Slider::LinearVertical
                                      : juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        sl.setRange (0.0, 1.0, 0.01);
        sl.setValue (1.0, juce::dontSendNotification);
        sl.setTooltip ("Slim size (0 = smallest/fastest, 1 = full quality). "
                       "Only active for NAM A2 (SlimmableContainer) models.");
        sl.setColour (juce::Slider::backgroundColourId,  juce::Colour (0xFF202630));
        sl.setColour (juce::Slider::trackColourId,        ns::Colours::tealAccent);
        sl.setColour (juce::Slider::thumbColourId,        ns::Colours::tealAccent.brighter (0.3f));
        sl.onValueChange = [this, i]
        {
            App::get().getAudioEngine().getNAM().setSlimSize (i, (float) slimSliders[i].getValue());
            refreshSlotLabels(); // update value display immediately while dragging
        };
        addChildComponent (sl); // hidden by default; shown in refreshSlotLabels when slimmable
    }

    blendPad.setCornerLabel (0, "A");
    blendPad.setCornerLabel (1, "B");
    blendPad.setCornerLabel (2, "C");
    blendPad.setCornerLabel (3, "D");
    blendPad.setPosition (0.5f, 0.5f, juce::dontSendNotification);
    blendPad.onDragStart = [] { App::get().pushUndoSnapshot(); };
    blendPad.onChanged = [] (float x, float y)
    {
        App::get().getAudioEngine().getNAM().setXYBlend (x, y);
    };
    // Do NOT call setXYBlend here: models aren't loaded yet during App::initialise()
    // and calling it with no models resets all weights to 0, silencing A/B/C.
    // The blend position is synced by refreshFromEngine() after models load.
    addAndMakeVisible (blendPad);

    // (status label removed — was unreadable behind the LCD inset.)

    refreshSlotLabels();
    startTimerHz (4);
}

NamLcdPanel::~NamLcdPanel()
{
    for (auto& b : slotButtons) b.setLookAndFeel (nullptr);
}

void NamLcdPanel::timerCallback()
{
    refreshSlotLabels();
}

void NamLcdPanel::chooseAndLoadNamFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> (
        "Load NAM model into slot " + juce::String ("ABCD").substring (slot, slot + 1),
        juce::File{}, "*.nam");

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this, slot] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{}) return;
        loadFile (slot, file);
    });
}

void NamLcdPanel::loadFile (int slot, const juce::File& file)
{
    if (file == juce::File{}) return;

    juce::String err;
    auto& nam = App::get().getAudioEngine().getNAM();
    if (nam.loadSlot (slot, file, err))
    {
        ns::RecentFiles::add ("NAM", file);
        refreshSlotLabels();
    }
    else
    {
        ns::ThemedAlerts::showWarning ("NAM load failed", err);
    }
}

void NamLcdPanel::showSlotMenu (int slot)
{
    juce::PopupMenu m;
    const auto label = juce::String ("ABCD").substring (slot, slot + 1);
    m.addSectionHeader ("NAM Slot " + label);
    m.addItem ("Choose file...", [this, slot] { chooseAndLoadNamFile (slot); });

    auto& nam = App::get().getAudioEngine().getNAM();
    if (nam.hasSlot (slot))
    {
        m.addItem ("Clear slot", [this, slot]
        {
            App::get().getAudioEngine().getNAM().clearSlot (slot);
            refreshSlotLabels();
        });
    }

    auto recents = ns::RecentFiles::load ("NAM");
    if (! recents.isEmpty())
    {
        m.addSeparator();
        juce::PopupMenu sub;
        for (auto& f : recents)
        {
            const auto path = f.getFullPathName();
            sub.addItem (f.getFileNameWithoutExtension(),
                         [this, slot, path] { loadFile (slot, juce::File (path)); });
        }
        sub.addSeparator();
        sub.addItem ("Clear list", [] { ns::RecentFiles::clear ("NAM"); });
        m.addSubMenu ("Recent NAMs", sub);
    }

    m.showMenuAsync (juce::PopupMenu::Options()
                       .withTargetComponent (&slotButtons[slot])
                       .withParentComponent (getTopLevelComponent()));
}

void NamLcdPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;

    // Right-click on a slot button → that slot's context menu.
    // Right-click anywhere else on the panel → the panel-level menu.
    for (int i = 0; i < 4; ++i)
    {
        if (e.eventComponent == &slotButtons[i])
        {
            showSlotMenu (i);
            return;
        }
    }
    showPanelMenu();
}

void NamLcdPanel::showPanelMenu()
{
    auto& nam = App::get().getAudioEngine().getNAM();
    juce::PopupMenu m;
    m.addSectionHeader ("NAM Amp");
    const bool norm = nam.isNormalized();
    m.addItem ("Normalize NAM output (-18 dBu)", true, norm,
               [&nam, norm] { nam.setNormalized (! norm); });
    m.addItem ("Bypass NAM", true, nam.isBypassed(),
               [&nam] { nam.setBypassed (! nam.isBypassed()); });
    m.showMenuAsync (juce::PopupMenu::Options()
                       .withTargetComponent (this)
                       .withParentComponent (getTopLevelComponent()));
}

void NamLcdPanel::refreshSlotLabels()
{
    auto& nam = App::get().getAudioEngine().getNAM();
    static const char* letters[] { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i)
    {
        const auto name = nam.getSlotName (i);
        const bool slimmableNow = nam.isSlotSlimmable (i);
        juce::String btnText = juce::String (letters[i]) + ": "
                               + (name.isEmpty() ? juce::String ("load model") : name);
        if (slimmableNow)
            btnText += "  " + juce::String (nam.getSlimSize (i), 2);
        slotButtons[i].setButtonText (btnText);
        // Highlight when active (loaded + not bypassed); dim when bypassed or empty.
        slotButtons[i].setToggleState (nam.hasSlot (i) && ! nam.isSlotBypassed (i),
                                       juce::dontSendNotification);
        blendPad.setCornerLoaded (i, nam.hasSlot (i));

        // Slim slider: show only for slimmable (NAM A2) models.
        slimSliders[i].setVisible (slimmableNow);
        if (slimmableNow)
        {
            // Sync slider value from engine without triggering onValueChange.
            const double engineVal = (double) nam.getSlimSize (i);
            if (std::abs (slimSliders[i].getValue() - engineVal) > 0.005)
                slimSliders[i].setValue (engineVal, juce::dontSendNotification);
        }
    }
}

void NamLcdPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Outer lavender frame
    g.setColour (ns::Colours::lavender);
    g.fillRoundedRectangle (bounds, (float) ns::UI::kPanelRadius);

    // Inner LCD area (dark inset)
    auto lcd = bounds.reduced (14.0f);
    g.setColour (juce::Colour (0xFF101218));
    g.fillRoundedRectangle (lcd, 12.0f);

    // Subtle inner highlight stroke
    g.setColour (juce::Colour (0x55FFFFFF));
    g.drawRoundedRectangle (lcd.reduced (1.0f), 11.0f, 1.0f);
}

void NamLcdPanel::resized()
{
    // Inset matches the LCD frame in paint().
    auto bounds = getLocalBounds().reduced (14 + 12);

    // Scale A/B button height and gap proportionally at small panel heights so
    // the blend pad retains enough vertical range to be usable (e.g. Pi 1024×600
    // where lcdH ≈ 224 px vs the design ≈ 472 px).
    // Design inner bounds height = 472 - 52 = 420 px; scale relative to that.
    // btnH min 18 px, gap min 6 px — keeps buttons legible at any size.
    const float hFrac = juce::jmin (1.0f, (float) bounds.getHeight() / 420.0f);
    const int   btnH  = juce::jmax (18, juce::roundToInt (36.0f * hFrac));
    const int   btnW  = 240;
    const int   sideW = 36;
    const int   gap   = juce::jmax (6,  juce::roundToInt (12.0f * hFrac));

    const int cx = bounds.getCentreX();

    // Slot buttons: EXACTLY as before — no size changes.
    slotButtons[0].setBounds (cx - btnW / 2, bounds.getY(), btnW, btnH);
    slotButtons[1].setBounds (cx - btnW / 2, bounds.getBottom() - btnH, btnW, btnH);
    slotButtons[2].setBounds (bounds.getX(), bounds.getY(), sideW, bounds.getHeight());
    slotButtons[3].setBounds (bounds.getRight() - sideW, bounds.getY(), sideW, bounds.getHeight());

    // Slim sliders live inside the existing 12-px gaps — no button is resized.
    //   A: horizontal strip in the gap below A button.
    slimSliders[0].setBounds (cx - btnW / 2,  bounds.getY() + btnH, btnW, gap);
    //   B: horizontal strip in the gap above B button.
    slimSliders[1].setBounds (cx - btnW / 2,  bounds.getBottom() - btnH - gap, btnW, gap);
    //   C: vertical strip in the gap between the C button and the blend pad (left side).
    slimSliders[2].setBounds (bounds.getX() + sideW, bounds.getY(), gap, bounds.getHeight());
    //   D: vertical strip in the gap between the blend pad and the D button (right side).
    slimSliders[3].setBounds (bounds.getRight() - sideW - gap, bounds.getY(), gap, bounds.getHeight());

    // Blend pad: IDENTICAL bounds to the original — no change.
    blendPad.setBounds (bounds.getX() + sideW + gap,
                        bounds.getY() + btnH  + gap,
                        bounds.getWidth()  - 2 * (sideW + gap),
                        bounds.getHeight() - 2 * (btnH  + gap));

    statusLabel.setBounds (0, 0, 0, 0);
}

void NamLcdPanel::refreshFromEngine()
{
    auto& nam = App::get().getAudioEngine().getNAM();
    // The blend pad now uses edge-midpoint anchors with a non-linear weight
    // formula, so we can't recover (x,y) cleanly from per-slot weights. Read
    // the last-commanded XY directly from the engine instead — this keeps
    // the puck in sync with preset recalls that go via setXYBlend().
    blendPad.setPosition (nam.getXYBlendX(), nam.getXYBlendY(),
                          juce::dontSendNotification);
    refreshSlotLabels();
}
