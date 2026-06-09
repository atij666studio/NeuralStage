#include "ThemedAlerts.h"
#include "../Styles/Colours.h"

namespace ns::ThemedAlerts
{
    //==============================================================================
    // Shared themed panel base. Paints a dark rounded card with the same
    // palette as the rest of the app, hosts a title strip + message body,
    // and exposes a row at the bottom for child controls (OK, Cancel, etc.).
    //==============================================================================
    namespace
    {
        constexpr int kPad        = 22;
        constexpr int kTitleH     = 28;
        constexpr int kButtonH    = 32;
        constexpr int kButtonGap  = 10;
        constexpr int kCornerR    = 10;

        enum Accent { AccentInfo, AccentWarn, AccentInput };

        juce::Colour stripeFor (Accent a)
        {
            switch (a)
            {
                case AccentWarn:  return ns::Colours::yellow;
                case AccentInput: return ns::Colours::accent;
                case AccentInfo:
                default:          return ns::Colours::accentGlow;
            }
        }

        /** Themed flat button. Uses ns::Colours, no LookAndFeel needed. */
        class ChipButton : public juce::Button
        {
        public:
            ChipButton (const juce::String& label, bool primary_)
                : juce::Button (label), primary (primary_)
            {
                setMouseCursor (juce::MouseCursor::PointingHandCursor);
            }

            void paintButton (juce::Graphics& g, bool over, bool down) override
            {
                auto r = getLocalBounds().toFloat().reduced (0.5f);
                auto fill = primary
                                ? (down ? ns::Colours::accent
                                        : over ? ns::Colours::accentHover
                                               : ns::Colours::accent)
                                : (down ? juce::Colour (0xFF1A1A22)
                                        : over ? juce::Colour (0xFF24242E)
                                               : juce::Colour (0xFF1A1A22));
                g.setColour (fill);
                g.fillRoundedRectangle (r, 6.0f);

                g.setColour (primary ? ns::Colours::accentGlow
                                     : juce::Colour (0xFF3A3A48));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);

                g.setColour (ns::Colours::textPrimary);
                g.setFont (juce::Font (juce::FontOptions (13.5f).withStyle ("Bold")));
                g.drawFittedText (getButtonText(), getLocalBounds(),
                                  juce::Justification::centred, 1);
            }

        private:
            bool primary;
        };

        /** Themed modal panel. Added to the desktop with NO native title
            bar -- everything is custom drawn. Esc + click-outside-card +
            close X all dismiss with result == 0. Button callbacks supply
            their own result codes. */
        class ThemedPanel : public juce::Component
        {
        public:
            ThemedPanel (Accent a,
                         juce::String titleText_,
                         juce::String messageText_)
                : accent (a),
                  titleText (std::move (titleText_)),
                  messageText (std::move (messageText_))
            {
                setOpaque (false);
                setWantsKeyboardFocus (true);
            }

            void paint (juce::Graphics& g) override
            {
                // Full-bleed semi-transparent scrim behind the card.
                g.fillAll (juce::Colour (0x99000000));

                const auto card = cardBounds().toFloat();

                // Card shadow.
                for (int i = 6; i >= 1; --i)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.06f));
                    g.fillRoundedRectangle (card.expanded ((float) i), (float) kCornerR + i);
                }

                // Card body.
                g.setColour (juce::Colour (0xFF14141A));
                g.fillRoundedRectangle (card, (float) kCornerR);
                g.setColour (juce::Colour (0xFF2A2A36));
                g.drawRoundedRectangle (card, (float) kCornerR, 1.0f);

                // Left accent stripe.
                g.setColour (stripeFor (accent));
                juce::Path stripe;
                stripe.addRoundedRectangle (card.getX(), card.getY(),
                                            4.0f, card.getHeight(),
                                            (float) kCornerR, (float) kCornerR,
                                            true, false, true, false);
                g.fillPath (stripe);

                // Title.
                auto inner = card.reduced ((float) kPad).withTrimmedLeft (8.0f);
                g.setColour (ns::Colours::textPrimary);
                g.setFont (juce::Font (juce::FontOptions (17.0f).withStyle ("Bold")));
                g.drawText (titleText,
                            inner.removeFromTop ((float) kTitleH),
                            juce::Justification::centredLeft, true);

                inner.removeFromTop (10.0f);

                // Reserve space at the bottom for the button row + any extra
                // controls (text editor in the input variant).
                const float bottomReserve = (float) (kButtonH + reservedExtraBottom + 14);
                auto bodyArea = inner.withTrimmedBottom (bottomReserve);

                // Message body.
                g.setColour (ns::Colours::textSecondary);
                g.setFont (juce::Font (juce::FontOptions (14.0f)));
                g.drawFittedText (messageText, bodyArea.toNearestInt(),
                                  juce::Justification::topLeft, 12);
            }

            void resized() override
            {
                const auto card  = cardBounds();
                auto       inner = card.reduced (kPad).withTrimmedLeft (8);

                // Button row sits at the very bottom of the card.
                auto row = inner.removeFromBottom (kButtonH);
                int x = row.getRight();
                for (int i = (int) buttons.size() - 1; i >= 0; --i)
                {
                    const int w = juce::jmax (84, buttons[(size_t) i]->getButtonText().length() * 11 + 28);
                    x -= w;
                    buttons[(size_t) i]->setBounds (x, row.getY(), w, row.getHeight());
                    x -= kButtonGap;
                }

                if (extraComponent != nullptr)
                {
                    auto extraRow = inner.removeFromBottom (reservedExtraBottom + 6)
                                          .withTrimmedTop (6);
                    extraComponent->setBounds (extraRow);
                }
            }

            bool keyPressed (const juce::KeyPress& k) override
            {
                if (k == juce::KeyPress::escapeKey)  { dismissWith (0); return true; }
                if (k == juce::KeyPress::returnKey)
                {
                    // Default = primary (rightmost) button.
                    if (! buttons.empty())
                    {
                        buttons.back()->triggerClick();
                        return true;
                    }
                }
                return juce::Component::keyPressed (k);
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                // Click outside the card dismisses with 0.
                if (! cardBounds().contains (e.getPosition()))
                    dismissWith (0);
            }

            void addButton (const juce::String& label, int resultCode,
                            std::function<void()> onClick, bool primary = false)
            {
                auto b = std::make_unique<ChipButton> (label, primary);
                b->onClick = [this, resultCode, cb = std::move (onClick)]
                {
                    if (cb) cb();
                    dismissWith (resultCode);
                };
                addAndMakeVisible (b.get());
                buttons.push_back (std::move (b));
            }

            void setExtraComponent (juce::Component* c, int reservedHeight)
            {
                extraComponent = c;
                reservedExtraBottom = reservedHeight;
                if (c != nullptr)
                    addAndMakeVisible (c);
            }

            /** Call once after construction to size + show. */
            void launch (int wPref = 460, int hPref = 220)
            {
                // Cover the whole desktop so the scrim is visible
                // everywhere the user can click. Card itself is centred.
                if (auto* mainDisplay = juce::Desktop::getInstance()
                                            .getDisplays().getPrimaryDisplay())
                {
                    const auto area = mainDisplay->userArea;
                    setBounds (area);
                }
                else
                {
                    setBounds (0, 0, 1280, 800);
                }

                desiredCardSize = { wPref, hPref };

                addToDesktop (juce::ComponentPeer::windowIsTemporary
                              | juce::ComponentPeer::windowHasDropShadow);
                setVisible (true);
                toFront (true);
                grabKeyboardFocus();
                enterModalState (true, nullptr, false);
                resized();
            }

        private:
            juce::Rectangle<int> cardBounds() const
            {
                const int w = desiredCardSize.getX();
                const int h = desiredCardSize.getY();
                return { (getWidth() - w) / 2, (getHeight() - h) / 2 - 30, w, h };
            }

            void dismissWith (int result)
            {
                if (dismissed) return;
                dismissed = true;
                exitModalState (result);
                setVisible (false);
                removeFromDesktop();
                // Defer delete so the click event finishes unwinding first.
                juce::MessageManager::callAsync ([this] { delete this; });
            }

            Accent accent;
            juce::String titleText, messageText;
            std::vector<std::unique_ptr<ChipButton>> buttons;
            juce::Component* extraComponent { nullptr };
            int reservedExtraBottom { 0 };
            juce::Point<int> desiredCardSize { 460, 220 };
            bool dismissed { false };
        };
    }

    //==============================================================================
    void showInfo (const juce::String& title,
                   const juce::String& message,
                   std::function<void()> onClose)
    {
        auto* p = new ThemedPanel (AccentInfo, title, message);
        p->addButton ("OK", 1, std::move (onClose), true);
        p->launch (480, 230);
    }

    void showWarning (const juce::String& title,
                      const juce::String& message,
                      std::function<void()> onClose)
    {
        auto* p = new ThemedPanel (AccentWarn, title, message);
        p->addButton ("OK", 1, std::move (onClose), true);
        p->launch (480, 230);
    }

    void showQuestion (const juce::String& title,
                       const juce::String& message,
                       const juce::String& yesLabel,
                       const juce::String& noLabel,
                       std::function<void(bool)> onChoice)
    {
        auto* p = new ThemedPanel (AccentInput, title, message);

        // Wrap the boolean callback in a shared_ptr so both buttons + the
        // dismiss path can each invoke it at most once.
        auto cbHolder = std::make_shared<std::function<void(bool)>> (std::move (onChoice));
        auto fired    = std::make_shared<bool> (false);
        auto fire = [cbHolder, fired] (bool v)
        {
            if (*fired) return;
            *fired = true;
            if (*cbHolder) (*cbHolder) (v);
        };

        p->addButton (noLabel,  0, [fire] { fire (false); }, false);
        p->addButton (yesLabel, 1, [fire] { fire (true);  }, true);
        p->launch (520, 260);
    }

    void showTextInput (const juce::String& title,
                        const juce::String& prompt,
                        const juce::String& initialText,
                        std::function<void(juce::String)> onSubmit)
    {
        auto* p = new ThemedPanel (AccentInput, title, prompt);

        auto* editor = new juce::TextEditor();
        editor->setText (initialText, juce::dontSendNotification);
        editor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF1F1F28));
        editor->setColour (juce::TextEditor::textColourId,        ns::Colours::textPrimary);
        editor->setColour (juce::TextEditor::outlineColourId,     juce::Colour (0xFF3A3A48));
        editor->setColour (juce::TextEditor::highlightColourId,   ns::Colours::accent.withAlpha (0.4f));
        editor->setColour (juce::TextEditor::focusedOutlineColourId, ns::Colours::accentGlow);
        editor->setFont (juce::Font (juce::FontOptions (14.0f)));
        editor->setJustification (juce::Justification::centredLeft);
        editor->setIndents (8, 4);
        editor->setSelectAllWhenFocused (true);
        p->setExtraComponent (editor, 30);

        auto cbHolder = std::make_shared<std::function<void(juce::String)>> (std::move (onSubmit));
        auto fired    = std::make_shared<bool> (false);
        auto fire = [cbHolder, fired] (juce::String s)
        {
            if (*fired) return;
            *fired = true;
            if (*cbHolder) (*cbHolder) (std::move (s));
        };

        p->addButton ("Cancel", 0, [fire]            { fire ({}); }, false);
        p->addButton ("OK",     1, [fire, editor]    { fire (editor->getText().trim()); }, true);

        // editor->onReturnKey defaults to nothing -- wire it to OK.
        editor->onReturnKey = [fire, editor] { fire (editor->getText().trim()); };

        p->launch (480, 240);
        juce::MessageManager::callAsync ([editor]
        {
            editor->grabKeyboardFocus();
            editor->selectAll();
        });
    }
}
