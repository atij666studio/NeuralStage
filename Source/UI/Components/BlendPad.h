#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** 2D blend pad with 4 corner labels (A/B/C/D).
 *  Drag the puck; emits onChanged(x, y) in [0..1]².
 *  A=top-left, B=top-right, C=bottom-left, D=bottom-right.
 */
class BlendPad : public juce::Component
{
public:
    BlendPad();
    ~BlendPad() override = default;

    void paint   (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    void setPosition (float x, float y, juce::NotificationType n = juce::sendNotification);
    void setCornerLabel (int idx, const juce::String& text); // 0..3 = A..D
    void setCornerLoaded (int idx, bool loaded);

    float getX() const noexcept { return xN; }
    float getY() const noexcept { return yN; }

    std::function<void (float, float)> onChanged;
    std::function<void()>              onDragStart;

private:
    void updateFromMouse (const juce::MouseEvent&);

    float xN { 0.0f }, yN { 0.0f };
    juce::String labels[4]   { "A", "B", "C", "D" };
    bool         loaded[4]   { false, false, false, false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlendPad)
};
