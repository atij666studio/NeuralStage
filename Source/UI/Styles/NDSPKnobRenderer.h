#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** NDSP-style rotary knob renderer (ported from AtiNAMatiC).
 *  Outer tick marks, inset track, LED-style coloured arc with bloom,
 *  brushed-metal cap with specular highlight, coloured pointer + tip dot,
 *  bipolar 12-o'clock detection.
 */
namespace ndsp
{
    inline void drawProRotary (juce::Graphics& g,
                               int x, int y, int w, int h,
                               float sliderPos,
                               float startAngle, float endAngle,
                               juce::Slider& slider,
                               juce::Colour arcColour,
                               bool  forceBipolar = false,
                               float bipolarZeroPos = 0.5f)
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h)
                          .reduced (4.0f);
        const float radius  = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float centreX = bounds.getCentreX();
        const float centreY = bounds.getCentreY();
        const float currentAngle = startAngle + sliderPos * (endAngle - startAngle);

        const bool  hover     = slider.isMouseOver (true) || slider.isMouseButtonDown();
        const float arcRadius = radius - 2.5f;
        const float arcThick  = juce::jmax (3.0f, radius * 0.10f);

        // Auto-bipolar detection (truly symmetric ranges only).
        bool isBipolar = forceBipolar;
        float zeroPos  = bipolarZeroPos;
        if (! forceBipolar)
        {
            const double absMin = std::abs (slider.getMinimum());
            const double absMax = std::abs (slider.getMaximum());
            if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                && std::abs (absMin - absMax) < (absMax * 0.05))
            {
                isBipolar = true;
                zeroPos = (float) ((-slider.getMinimum())
                                   / (slider.getMaximum() - slider.getMinimum()));
            }
        }

        // Tick marks.
        {
            const int   nTicks    = 11;
            const float tickInner = arcRadius + arcThick * 0.55f;
            const float tickOuter = arcRadius + arcThick * 1.25f;
            g.setColour (juce::Colour (0xFF2A2A2E));
            for (int i = 0; i < nTicks; ++i)
            {
                const float t = (float) i / (float) (nTicks - 1);
                const float a = startAngle + t * (endAngle - startAngle);
                const float ix = centreX + tickInner * std::sin (a);
                const float iy = centreY - tickInner * std::cos (a);
                const float ox = centreX + tickOuter * std::sin (a);
                const float oy = centreY - tickOuter * std::cos (a);
                g.drawLine (ix, iy, ox, oy, 1.2f);
            }
        }

        // Background track arc.
        juce::Path trackArc;
        trackArc.addCentredArc (centreX, centreY, arcRadius, arcRadius,
                                0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xFF1A1A1E));
        g.strokePath (trackArc, juce::PathStrokeType (arcThick + 1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xFF2E2E33));
        g.strokePath (trackArc, juce::PathStrokeType (arcThick,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Coloured value arc with bloom.
        auto drawValueArc = [&] (float a0, float a1)
        {
            if (std::abs (a1 - a0) < 0.005f) return;
            juce::Path va;
            va.addCentredArc (centreX, centreY, arcRadius, arcRadius,
                              0.0f, a0, a1, true);
            g.setColour (arcColour.withAlpha (hover ? 0.55f : 0.30f));
            g.strokePath (va, juce::PathStrokeType (arcThick + 5.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (arcColour.withAlpha (hover ? 0.85f : 0.55f));
            g.strokePath (va, juce::PathStrokeType (arcThick + 2.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (arcColour);
            g.strokePath (va, juce::PathStrokeType (arcThick,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        if (isBipolar)
        {
            const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);
            const float a0 = std::min (currentAngle, zeroAngle);
            const float a1 = std::max (currentAngle, zeroAngle);
            drawValueArc (a0, a1);
        }
        else if (sliderPos > 0.001f)
        {
            drawValueArc (startAngle, currentAngle);
        }

        // Brushed metal cap.
        const float knobRadius = radius - arcThick - 6.0f;
        const float kx = centreX - knobRadius;
        const float ky = centreY - knobRadius;
        const float kd = knobRadius * 2.0f;

        // Soft AO halo.
        for (int s = 0; s < 3; ++s)
        {
            const float spread = 1.0f + (float) s * 1.6f;
            const float alpha  = 0.20f - (float) s * 0.06f;
            g.setColour (juce::Colours::black.withAlpha (alpha));
            g.fillEllipse (kx - spread, ky + spread * 0.6f + 1.0f,
                           kd + spread * 2.0f, kd + spread * 2.0f);
        }

        juce::ColourGradient rimGrad (juce::Colour (0xFF4A4A52), centreX, centreY - knobRadius,
                                      juce::Colour (0xFF1C1C20), centreX, centreY + knobRadius,
                                      false);
        g.setGradientFill (rimGrad);
        g.fillEllipse (kx, ky, kd, kd);

        const float capR = knobRadius - 2.5f;
        const float capX = centreX - capR;
        const float capY = centreY - capR;
        juce::ColourGradient capGrad (juce::Colour (0xFF38383E), centreX, centreY - capR * 0.7f,
                                      juce::Colour (0xFF18181C), centreX, centreY + capR,
                                      false);
        g.setGradientFill (capGrad);
        g.fillEllipse (capX, capY, capR * 2.0f, capR * 2.0f);

        // Specular highlight.
        {
            juce::Path hl;
            hl.addEllipse (capX + capR * 0.18f, capY + capR * 0.10f,
                           capR * 1.64f, capR * 0.55f);
            g.setColour (juce::Colour (0x22FFFFFF));
            g.fillPath (hl);
        }

        // Inner shadow ring.
        g.setColour (juce::Colour (0x55000000));
        g.drawEllipse (capX + 0.5f, capY + 0.5f, capR * 2.0f - 1.0f, capR * 2.0f - 1.0f, 1.0f);

        // Pointer.
        const float pTipR  = capR * 0.92f;
        const float pBaseR = capR * 0.30f;
        const float sinA = std::sin (currentAngle);
        const float cosA = std::cos (currentAngle);
        const float tipX  = centreX + pTipR  * sinA;
        const float tipY  = centreY - pTipR  * cosA;
        const float baseX = centreX + pBaseR * sinA;
        const float baseY = centreY - pBaseR * cosA;

        g.setColour (juce::Colour (0xCC000000));
        g.drawLine (baseX, baseY + 1.0f, tipX, tipY + 1.0f, 3.5f);
        g.setColour (arcColour.brighter (0.10f));
        g.drawLine (baseX, baseY, tipX, tipY,
                    juce::jmax (2.5f, capR * 0.10f));
        g.setColour (juce::Colour (0xFFFFFFFF).withAlpha (0.85f));
        g.fillEllipse (tipX - 1.5f, tipY - 1.5f, 3.0f, 3.0f);

        // Centre well.
        g.setColour (juce::Colour (0xFF101013));
        g.fillEllipse (centreX - 2.5f, centreY - 2.5f, 5.0f, 5.0f);
        g.setColour (juce::Colour (0x33FFFFFF));
        g.drawEllipse (centreX - 2.5f, centreY - 2.5f, 5.0f, 5.0f, 0.5f);
    }
}
