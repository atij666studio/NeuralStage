#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/*  Themed in-app alert / confirm / input helpers.

    Replacement for juce::AlertWindow / juce::NativeMessageBox usage. All of
    these render a custom-drawn panel using ns::Colours (no native title bar,
    no JUCE-default LookAndFeel chrome) so popups visually belong to the app
    rather than the OS.

    Implementation detail: each helper creates a top-level Component added to
    the desktop without a native title bar. Click outside, Esc, the close X,
    or the OK button dismisses it. The callback (if supplied) runs on the
    message thread after dismissal and CANNOT cause the app to quit, even if
    the main window briefly loses focus during the modal -- the helpers do
    not interact with JUCEApplication's quit lifecycle.
*/
namespace ns::ThemedAlerts
{
    /** Plain info pop-up. `onClose` (optional) is invoked when the dialog
        is dismissed, with no argument. */
    void showInfo (const juce::String& title,
                   const juce::String& message,
                   std::function<void()> onClose = {});

    /** Warning pop-up (same as showInfo but with a yellow/red accent). */
    void showWarning (const juce::String& title,
                      const juce::String& message,
                      std::function<void()> onClose = {});

    /** Yes/No question. `onChoice(true)` for yes, `onChoice(false)` for
        no/cancel/Esc. */
    void showQuestion (const juce::String& title,
                       const juce::String& message,
                       const juce::String& yesLabel,
                       const juce::String& noLabel,
                       std::function<void(bool)> onChoice);

    /** Single-line text input. `onSubmit(text)` is invoked on OK with the
        trimmed contents; on Cancel / Esc the callback is invoked with an
        empty string. */
    void showTextInput (const juce::String& title,
                        const juce::String& prompt,
                        const juce::String& initialText,
                        std::function<void(juce::String)> onSubmit);
}
