#include "KnobComponent.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../Dialogs/ThemedAlerts.h"
#include "../../App.h"
#include "../../MIDI/MIDILearn.h"

KnobComponent::KnobComponent()
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (-150.0f),
                                juce::degreesToRadians ( 150.0f),
                                true);
    slider.setRange (0.0, 1.0, 0.0);
    slider.setValue (0.5);
    slider.addListener (this);
    addAndMakeVisible (slider);

    nameLabel.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, ns::Colours::textOnPanel);
    addAndMakeVisible (nameLabel);

    valueLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, ns::Colours::textOnPanel);
    addAndMakeVisible (valueLabel);

    // Right-click anywhere on the knob (label, value text, or slider itself)
    // pops the MIDI-learn menu when this knob has been bound via bindMidi().
    addMouseListener (this, true);

    updateValueText();
}

KnobComponent& KnobComponent::setLabel (const juce::String& text)
{
    nameLabel.setText (text, juce::dontSendNotification);
    return *this;
}

KnobComponent& KnobComponent::configure (double min, double max, double def,
                                         const juce::String& sx)
{
    slider.setRange (min, max, 0.0);
    slider.setValue (def, juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, def);
    suffix = sx.trim();
    defaultValue = def;
    hasDefault   = true;
    updateValueText();
    return *this;
}

KnobComponent& KnobComponent::setTooltipText (const juce::String& text)
{
    slider    .setTooltip (text);
    nameLabel .setTooltip (text);
    valueLabel.setTooltip (text);
    return *this;
}

void KnobComponent::sliderValueChanged (juce::Slider*)
{
    updateValueText();
    if (externalCb) externalCb (slider.getValue());
}

void KnobComponent::sliderDragStarted (juce::Slider*)
{
    // Capture an undo point at the start of every knob drag so the gesture
    // is reversible as a single step (one snapshot per drag, not per value).
    if (auto* app = dynamic_cast<App*> (juce::JUCEApplication::getInstance()))
        app->pushUndoSnapshot();
}

void KnobComponent::updateValueText()
{
    const double v = slider.getValue();
    juce::String s = juce::String (v, decimals);
    if (suffix.isNotEmpty()) s += suffix;
    valueLabel.setText (s, juce::dontSendNotification);
}

KnobComponent::~KnobComponent()
{
    slider.removeListener (this);
    slider.setLookAndFeel (nullptr);
    if (midiParamId.isNotEmpty())
    {
        // Detach by replacing with a no-op setter so any in-flight async
        // callback after we are destroyed doesn't touch us.
        if (auto* app = dynamic_cast<App*> (juce::JUCEApplication::getInstance()))
            app->getMIDILearn().registerParameter (midiParamId, midiDisplayName, [] (float) {});
    }
}

void KnobComponent::bindMidi (const juce::String& paramId, const juce::String& displayName)
{
    midiParamId     = paramId;
    midiDisplayName = displayName;

    auto& reg = App::get().getMIDILearn();
    juce::Component::SafePointer<KnobComponent> safe (this);
    reg.registerParameter (paramId, displayName,
        [safe] (float v01)
        {
            // 0..1 -> slider range. Posted on message thread by the registry.
            if (auto* k = safe.getComponent())
            {
                auto& s = k->getSlider();
                const double mapped = s.getMinimum() + (double) v01 * (s.getMaximum() - s.getMinimum());
                s.setValue (mapped, juce::sendNotificationSync);
            }
        });
}

void KnobComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }
    juce::Component::mouseDown (e);
}

void KnobComponent::promptForValue()
{
    const auto title  = nameLabel.getText().isNotEmpty() ? nameLabel.getText()
                                                         : juce::String ("Set value");
    const auto prompt = juce::String ("Enter new value")
                        + (suffix.isNotEmpty() ? juce::String (" (" + suffix.trim() + ")")
                                               : juce::String());
    juce::Component::SafePointer<KnobComponent> safe (this);
    const juce::String initial = juce::String (slider.getValue(), decimals);

    ns::ThemedAlerts::showTextInput (title, prompt, initial,
        [safe] (juce::String txt)
        {
            if (txt.isEmpty()) return;
            if (auto* k = safe.getComponent())
            {
                auto stripped = txt.upToFirstOccurrenceOf (k->suffix.trim(), false, true).trim();
                if (stripped.isEmpty()) stripped = txt;
                const double v = stripped.getDoubleValue();
                k->slider.setValue (juce::jlimit (k->slider.getMinimum(),
                                                   k->slider.getMaximum(), v),
                                    juce::sendNotificationSync);
            }
        });
}

void KnobComponent::showContextMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (nameLabel.getText().isNotEmpty() ? nameLabel.getText()
                                                         : (midiDisplayName.isNotEmpty() ? midiDisplayName
                                                                                         : juce::String ("Parameter")));

    juce::Component::SafePointer<KnobComponent> safe (this);

    if (hasDefault)
        m.addItem ("Reset to default (" + juce::String (defaultValue, decimals) + suffix + ")",
                   [safe] { if (auto* k = safe.getComponent())
                                k->slider.setValue (k->defaultValue, juce::sendNotificationSync); });

    m.addItem ("Type value...", [safe] { if (auto* k = safe.getComponent()) k->promptForValue(); });

    m.addItem ("Copy value",  [safe]
    {
        if (auto* k = safe.getComponent())
            juce::SystemClipboard::copyTextToClipboard (juce::String (k->slider.getValue(), k->decimals));
    });

    const auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
    bool clipIsNumber = false;
    {
        auto stripped = clip.upToFirstOccurrenceOf (suffix.trim(), false, true).trim();
        if (stripped.isEmpty()) stripped = clip;
        if (stripped.isNotEmpty() && (stripped.containsOnly ("-+0123456789.eE")))
            clipIsNumber = true;
    }
    m.addItem ("Paste value" + (clipIsNumber ? juce::String (" (" + clip + ")") : juce::String()),
               clipIsNumber, false,
               [safe, clip]
               {
                   if (auto* k = safe.getComponent())
                   {
                       auto stripped = clip.upToFirstOccurrenceOf (k->suffix.trim(), false, true).trim();
                       if (stripped.isEmpty()) stripped = clip;
                       const double v = stripped.getDoubleValue();
                       k->slider.setValue (juce::jlimit (k->slider.getMinimum(), k->slider.getMaximum(), v),
                                           juce::sendNotificationSync);
                   }
               });

    // ---- MIDI section, only when this knob has been bindMidi'd. ----
    if (midiParamId.isNotEmpty())
    {
        m.addSeparator();
        auto& reg = App::get().getMIDILearn();

        juce::String existing;
        for (auto& mp : reg.getMappings())
            if (mp.paramId == midiParamId)
            {
                existing = ((mp.type == MidiMsgType::CC)   ? juce::String ("CC ")
                          : (mp.type == MidiMsgType::Note) ? juce::String ("Note ")
                                                            : juce::String ("PC "))
                         + juce::String (mp.ccOrNote)
                         + (mp.channel == 0 ? " (Omni)" : " Ch " + juce::String (mp.channel));
                break;
            }

        if (existing.isNotEmpty())
            m.addItem ("Mapped: " + existing, false, false, [] {});

        const auto pid = midiParamId;
        if (reg.isLearning() && reg.currentLearnTarget() == pid)
            m.addItem ("Cancel learn", [&reg] { reg.cancelLearn(); });
        else
            m.addItem ("MIDI Learn (move a controller)", [&reg, pid] { reg.beginLearn (pid); });

        m.addItem ("Clear MIDI mapping", existing.isNotEmpty(), false,
                   [&reg, pid] { reg.clearMapping (pid); });
    }

    m.showMenuAsync ({});
}

void KnobComponent::paint (juce::Graphics&) {}

void KnobComponent::resized()
{
    auto r = getLocalBounds();
    nameLabel .setBounds (r.removeFromTop (ns::UI::kKnobNameH));
    valueLabel.setBounds (r.removeFromBottom (ns::UI::kKnobValueH));

    const int side = juce::jmin (r.getHeight(), r.getWidth());
    const int kx = r.getX() + (r.getWidth()  - side) / 2;
    const int ky = r.getY() + (r.getHeight() - side) / 2;
    slider.setBounds (kx, ky, side, side);
}
