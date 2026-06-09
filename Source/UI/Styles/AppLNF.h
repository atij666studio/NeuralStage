#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Colours.h"

namespace ns
{
    /** NAM-A2 inspired knob: white round body, soft drop shadow,
     *  thin black indicator line. No centre value text.
     */
    class NamA2KnobLNF : public juce::LookAndFeel_V4
    {
    public:
        NamA2KnobLNF() = default;

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float pos, float startAng, float endAng,
                               juce::Slider& /*s*/) override
        {
            // Target a fixed body diameter (92 px) on every knob regardless
            // of how large the slider component is.  For large cells (AmpKnobs
            // ~122 px) this pads out 15 px of shadow clearance on every side;
            // for smaller cells (SideRail ~100 px) it pads 4 px — enough for
            // the shadow to render without clipping.  Minimum reduction = 4 px
            // so we never overshoot on very small sliders.
            const float targetDiam  = 92.0f;
            const float available   = juce::jmin ((float) w, (float) h);
            const float reduceAmt   = juce::jmax (4.0f, (available - targetDiam) * 0.5f);
            const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                        (float) w, (float) h).reduced (reduceAmt);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const float cx = bounds.getCentreX();
            const float cy = bounds.getCentreY();
            const float ang = startAng + pos * (endAng - startAng);

            // Soft drop shadow — manual concentric ellipses with a mild
            // downward offset sized to stay within the slider component bounds.
            for (int i = 3; i >= 0; --i)
            {
                const float spread  = 2.0f + (float) i * 1.5f;
                const float offsetY = spread * 0.35f;   // small, stays in-bounds
                const float alpha   = 0.13f - (float) i * 0.025f;
                g.setColour (juce::Colours::black.withAlpha (alpha));
                g.fillEllipse (cx - radius - spread * 0.4f,
                               cy - radius + offsetY,
                               radius * 2.0f + spread * 0.8f,
                               radius * 2.0f + spread * 0.8f);
            }

            // Body — subtle vertical gradient (white → very light grey).
            juce::ColourGradient bodyGrad (juce::Colours::white, cx, cy - radius,
                                           juce::Colour (0xFFE6E6EC), cx, cy + radius,
                                           false);
            g.setGradientFill (bodyGrad);
            g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

            // Inner soft inset.
            g.setColour (juce::Colours::black.withAlpha (0.06f));
            g.drawEllipse (cx - radius + 0.5f, cy - radius + 0.5f,
                           radius * 2.0f - 1.0f, radius * 2.0f - 1.0f, 1.0f);

            // Indicator line.
            const float indL  = radius * 0.85f;
            const float indL0 = radius * 0.15f;
            const float sinA = std::sin (ang);
            const float cosA = std::cos (ang);
            const float x0 = cx + indL0 * sinA;
            const float y0 = cy - indL0 * cosA;
            const float x1 = cx + indL  * sinA;
            const float y1 = cy - indL  * cosA;

            g.setColour (juce::Colour (0xFF1A1A1F));
            g.drawLine (x0, y0, x1, y1, juce::jmax (1.6f, radius * 0.05f));
        }
    };

    inline NamA2KnobLNF& a2KnobLNF() { static NamA2KnobLNF inst; return inst; }

    /** Draws the NAM-A2 segmented chip button background and label. */
    inline void drawChipButton (juce::Graphics& g, juce::Rectangle<float> r,
                                bool selected, const juce::String& text,
                                float corner = 8.0f)
    {
        g.setColour (selected ? ns::Colours::chipSel : ns::Colours::chipUnsel);
        g.fillRoundedRectangle (r, corner);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle (selected ? "Bold" : "")));
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1);
    }
}
