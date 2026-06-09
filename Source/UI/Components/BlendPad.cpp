#include "BlendPad.h"
#include "../Styles/Colours.h"

BlendPad::BlendPad() = default;

void BlendPad::setCornerLabel (int idx, const juce::String& text)
{
    if (idx >= 0 && idx < 4) { labels[idx] = text; repaint(); }
}

void BlendPad::setCornerLoaded (int idx, bool l)
{
    if (idx >= 0 && idx < 4) { loaded[idx] = l; repaint(); }
}

void BlendPad::setPosition (float x, float y, juce::NotificationType n)
{
    xN = juce::jlimit (0.0f, 1.0f, x);
    yN = juce::jlimit (0.0f, 1.0f, y);
    repaint();
    if (n == juce::sendNotification && onChanged) onChanged (xN, yN);
}

void BlendPad::mouseDown (const juce::MouseEvent& e)
{
    if (onDragStart) onDragStart();
    updateFromMouse (e);
}
void BlendPad::mouseDrag (const juce::MouseEvent& e) { updateFromMouse (e); }

void BlendPad::updateFromMouse (const juce::MouseEvent& e)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f);
    const float nx = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
    const float ny = juce::jlimit (0.0f, 1.0f, (e.position.y - r.getY()) / r.getHeight());
    xN = nx; yN = ny;
    repaint();
    if (onChanged) onChanged (xN, yN);
}

void BlendPad::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    auto r    = full.reduced (8.0f);

    // Inset pad area — noticeably brighter than the surrounding LCD,
    // with a clear 2px lavender border so the boundaries are obvious.
    g.setColour (juce::Colour (0xFF2C2F3A));
    g.fillRoundedRectangle (full, 10.0f);

    g.setColour (ns::Colours::lavender.withAlpha (0.55f));
    g.drawRoundedRectangle (full.reduced (1.0f), 9.0f, 2.0f);

    // Grid — brighter centre cross, softer quarter lines.
    g.setColour (juce::Colour (0xFF5A607A));
    g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom(), 1.4f);
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.4f);

    g.setColour (juce::Colour (0xFF3F4458));
    const float qx1 = r.getX() + r.getWidth()  * 0.25f;
    const float qx2 = r.getX() + r.getWidth()  * 0.75f;
    const float qy1 = r.getY() + r.getHeight() * 0.25f;
    const float qy2 = r.getY() + r.getHeight() * 0.75f;
    g.drawLine (qx1, r.getY(), qx1, r.getBottom(), 1.0f);
    g.drawLine (qx2, r.getY(), qx2, r.getBottom(), 1.0f);
    g.drawLine (r.getX(), qy1, r.getRight(), qy1, 1.0f);
    g.drawLine (r.getX(), qy2, r.getRight(), qy2, 1.0f);

    // Corner labels (drawn at the EDGE MIDPOINTS — that's where the four
    // NAM anchors now live; corner positions are blends, centre is equal).
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    auto drawAnchor = [&] (int idx, juce::Justification j, juce::Rectangle<float> area)
    {
        g.setColour (loaded[idx] ? ns::Colours::tealLight
                                 : juce::Colour (0xFF8088A0));
        g.drawFittedText (labels[idx], area.toNearestInt(), j, 1);
    };
    const float pad   = 4.0f;
    const float bandH = 18.0f;
    const float bandW = 22.0f;
    auto fr = getLocalBounds().toFloat().reduced (4.0f);
    // Anchor layout matches the surrounding NamLcdPanel buttons:
    // A = top centre, B = bottom centre, C = left centre, D = right centre.
    drawAnchor (0, juce::Justification::centredTop,
                juce::Rectangle<float> (fr.getCentreX() - 40.0f, fr.getY() + pad,
                                        80.0f, bandH));
    drawAnchor (1, juce::Justification::centredBottom,
                juce::Rectangle<float> (fr.getCentreX() - 40.0f, fr.getBottom() - bandH - pad,
                                        80.0f, bandH));
    drawAnchor (2, juce::Justification::centredLeft,
                juce::Rectangle<float> (fr.getX() + pad, fr.getCentreY() - 9.0f,
                                        bandW, bandH));
    drawAnchor (3, juce::Justification::centredRight,
                juce::Rectangle<float> (fr.getRight() - bandW - pad, fr.getCentreY() - 9.0f,
                                        bandW, bandH));

    // Small anchor dots at the actual XY each NAM responds to.
    auto padArea = getLocalBounds().toFloat().reduced (8.0f);
    const juce::Point<float> anchors[4] = {
        { padArea.getCentreX(), padArea.getY()       }, // A top
        { padArea.getCentreX(), padArea.getBottom()  }, // B bottom
        { padArea.getX(),       padArea.getCentreY() }, // C left
        { padArea.getRight(),   padArea.getCentreY() }  // D right
    };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour ((loaded[i] ? ns::Colours::tealAccent
                                : juce::Colour (0xFF606878)).withAlpha (0.85f));
        g.fillEllipse (anchors[i].x - 4.0f, anchors[i].y - 4.0f, 8.0f, 8.0f);
    }

    // Puck.
    auto pad2 = getLocalBounds().toFloat().reduced (8.0f);
    const float px = pad2.getX() + xN * pad2.getWidth();
    const float py = pad2.getY() + yN * pad2.getHeight();
    const float pr = 14.0f;

    g.setColour (ns::Colours::tealLight.withAlpha (0.30f));
    g.fillEllipse (px - pr * 1.8f, py - pr * 1.8f, pr * 3.6f, pr * 3.6f);
    g.setColour (ns::Colours::tealAccent);
    g.fillEllipse (px - pr, py - pr, pr * 2.0f, pr * 2.0f);
    g.setColour (juce::Colours::white);
    g.drawEllipse (px - pr, py - pr, pr * 2.0f, pr * 2.0f, 2.0f);
}
