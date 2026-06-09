#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Colours.h"

namespace ns
{
    //==========================================================================
    /** Custom LookAndFeel that paints DocumentWindow / DialogWindow title
        bars and close buttons in the NeuralStage theme. Used in place of the
        native OS title bar so secondary windows (Presets, MIDI, About,
        hosted plugin editors, etc.) visually belong to the app.

        Only window chrome is overridden; every other widget continues to use
        whatever LookAndFeel was already set on it. No audio or DSP code
        path is touched.
    */
    class AppWindowLNF : public juce::LookAndFeel_V4
    {
    public:
        AppWindowLNF()
        {
            // Background fill the V4 base uses when no explicit colour is
            // set on a window — keep it in the app palette so the brief
            // moment between window creation and content paint is dark.
            setColour (juce::ResizableWindow::backgroundColourId, ns::Colours::background);
            setColour (juce::DocumentWindow::textColourId,        ns::Colours::textPrimary);
        }

        //----------------------------------------------------------------------
        // Title bar paint
        //----------------------------------------------------------------------
        void drawDocumentWindowTitleBar (juce::DocumentWindow& window,
                                         juce::Graphics& g,
                                         int w, int h,
                                         int titleSpaceX, int titleSpaceW,
                                         const juce::Image* icon,
                                         bool drawTitleTextOnLeft) override
        {
            const auto area = juce::Rectangle<int> (0, 0, w, h);

            // Background — slightly darker than the app body so the bar reads
            // as a frame. Subtle 1px accent stripe along the bottom edge.
            g.setColour (juce::Colour (0xFF0A0A0E));
            g.fillRect (area);

            g.setColour (ns::Colours::accent.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<int> (0, h - 1, w, 1));

            // Optional icon (left of the title)
            int textLeft = titleSpaceX;
            if (icon != nullptr)
            {
                const int iconH = h - 8;
                g.drawImageWithin (*icon, 8, 4, iconH, iconH,
                                   juce::RectanglePlacement::xLeft
                                 | juce::RectanglePlacement::yMid
                                 | juce::RectanglePlacement::onlyReduceInSize);
                textLeft = juce::jmax (textLeft, iconH + 14);
            }

            // Title text
            const auto title = window.getName();
            g.setColour (ns::Colours::textPrimary);
            g.setFont (juce::Font (juce::FontOptions ((float) h * 0.55f).withStyle ("Bold")));

            const auto justify = drawTitleTextOnLeft
                                    ? juce::Justification::centredLeft
                                    : juce::Justification::centred;

            g.drawText (title,
                        juce::Rectangle<int> (textLeft, 0, titleSpaceW, h),
                        justify, true);
        }

        //----------------------------------------------------------------------
        // Close / minimise / maximise buttons
        //----------------------------------------------------------------------
        juce::Button* createDocumentWindowButton (int buttonType) override
        {
            return new TitleBarButton (buttonType);
        }

        void positionDocumentWindowButtons (juce::DocumentWindow& /*w*/,
                                            int titleBarX, int titleBarY,
                                            int titleBarW, int titleBarH,
                                            juce::Button* minButton,
                                            juce::Button* maxButton,
                                            juce::Button* closeButton,
                                            bool positionTitleBarButtonsOnLeft) override
        {
            const int size = titleBarH - 6;
            const int gap  = 4;
            int x = positionTitleBarButtonsOnLeft
                        ? titleBarX + gap
                        : titleBarX + titleBarW - size - gap;
            const int step = positionTitleBarButtonsOnLeft ? (size + gap) : -(size + gap);

            for (auto* b : { closeButton, maxButton, minButton })
            {
                if (b == nullptr) continue;
                b->setBounds (x, titleBarY + 3, size, size);
                x += step;
            }
        }

    private:
        //----------------------------------------------------------------------
        // Themed title-bar button. Hover = accent tint, close = red on hover.
        //----------------------------------------------------------------------
        class TitleBarButton : public juce::Button
        {
        public:
            explicit TitleBarButton (int btnType)
                : juce::Button (juce::String()), type (btnType) {}

            void paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
            {
                const auto bounds = getLocalBounds().toFloat().reduced (1.5f);

                // Hover fill
                juce::Colour fill;
                if (type == juce::DocumentWindow::closeButton)
                    fill = isMouseDown ? juce::Colour (0xFFCC2A2A)
                                       : (isMouseOver ? juce::Colour (0xFFE05050)
                                                      : juce::Colours::transparentBlack);
                else
                    fill = isMouseDown ? ns::Colours::accent
                                       : (isMouseOver ? ns::Colours::accent.withAlpha (0.45f)
                                                      : juce::Colours::transparentBlack);

                if (fill.getAlpha() > 0)
                {
                    g.setColour (fill);
                    g.fillRoundedRectangle (bounds, 3.0f);
                }

                // Glyph
                const auto cx = bounds.getCentreX();
                const auto cy = bounds.getCentreY();
                const auto r  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.28f;

                g.setColour (isMouseOver ? juce::Colours::white
                                         : ns::Colours::textPrimary);

                if (type == juce::DocumentWindow::closeButton)
                {
                    // X
                    juce::Path p;
                    p.startNewSubPath (cx - r, cy - r);
                    p.lineTo          (cx + r, cy + r);
                    p.startNewSubPath (cx + r, cy - r);
                    p.lineTo          (cx - r, cy + r);
                    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                                  juce::PathStrokeType::rounded));
                }
                else if (type == juce::DocumentWindow::minimiseButton)
                {
                    g.drawLine (cx - r, cy + r * 0.4f, cx + r, cy + r * 0.4f, 1.6f);
                }
                else // maximise / restore
                {
                    g.drawRect (juce::Rectangle<float> (cx - r, cy - r, r * 2.0f, r * 2.0f), 1.4f);
                }
            }

        private:
            int type;
        };
    };

    /** Process-wide single instance — registered on the windows we own. */
    inline AppWindowLNF& appWindowLNF()
    {
        static AppWindowLNF lnf;
        return lnf;
    }
}
