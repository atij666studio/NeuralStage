#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../PluginHost/PluginCategory.h"

/** A single signal-chain block button (GATE / COMP / DRIVE / NAM / IR / EQ / MOD / ...). */
class SignalChainButton : public juce::Button
{
public:
    SignalChainButton (const juce::String& label);

    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    void mouseDown   (const juce::MouseEvent&) override;
    void mouseDrag   (const juce::MouseEvent&) override;
    void mouseUp     (const juce::MouseEvent&) override;

    void setActive   (bool a)            { active = a; repaint(); }
    void setBadgeCount (int n)           { badge  = n; repaint(); }
    int  getBadge     () const           { return badge; }
    void setBypassed (bool b)            { bypassed = b; repaint(); }
    bool isBypassed () const             { return bypassed; }
    void setBlockId  (int id)            { blockId = id; repaint(); }

    /** When non-empty, the chip renders the loaded plugin's display name
     *  underneath the category label (two-line layout). Empty string
     *  reverts to single centred category label. */
    void setLoadedName (const juce::String& n) { loadedName = n; repaint(); }

    /** Fired on right-click / popup-modifier click. Separate from onClick so
     *  that the left-click (toggle bypass) and right-click (open editor)
     *  semantics never bleed into each other. */
    std::function<void()> onRightClick;

    /** Drag-finish notification. Fired after the user has dragged this chip
     *  more than a few pixels and released the mouse. Used by the parent
     *  SignalChainBar to support repositioning the MOD block between Pre
     *  and Post halves of the chain. screenPos is the mouse position at
     *  release time, in screen coords. */
    std::function<void (juce::Point<int> screenPos)> onDragEnd;

    /** Enable mouse-drag tracking for this chip (only chips that are
     *  user-movable opt in -- everything else stays click/right-click only
     *  to avoid accidental drags). */
    void setDraggable (bool b) { draggable = b; }

private:
    juce::String label;
    juce::String loadedName;
    bool         active     { false };
    bool         bypassed   { false };
    int          badge      { 0 };
    int          blockId    { -1 };
    bool         draggable  { false };
    bool         dragging   { false };
};

/** Horizontal strip of clickable signal-chain blocks (full guitar signal flow). */
class SignalChainBar : public juce::Component
{
public:
    enum BlockId
    {
        Gate = 0,
        Comp,
        Drive,
        NamAmp,
        IrCab,
        Eq,
        Mod,
        Delay,
        Reverb,
        Limiter,
        Fx,
        NumBlocks
    };

    SignalChainBar();
    ~SignalChainBar() override = default;

    void paint   (juce::Graphics&) override;
    void resized() override;

    /** Refreshes per-block badge counts (number of plugins / loaded state). */
    void refreshBadges();

    /** Whether the MOD block has been dragged into the Pre-FX half of the
     *  bar (between DRIVE and NAM) instead of its default Post-FX position
     *  (between IR and DELAY). Affects:
     *    - visual chip order in this bar
     *    - which PluginChain (pre vs post) the MOD picker writes into when
     *      the user adds a new modulation plugin (see MainComponent's
     *      entries[] table -- it consults this flag)
     *  Existing modulation plugins are NOT migrated between chains; only
     *  newly-added ones land in the new chain. */
    bool isModBeforeNam() const noexcept   { return modBeforeNam; }
    void setModBeforeNam (bool b);
    std::function<void (bool beforeNam)> onModPositionChanged;

    /** Click a block to open its load/manage popup. Wired by MainComponent
     *  to the EDIT button on the right edge of the chain, not to the blocks
     *  themselves anymore. */
    std::function<void (int blockId, juce::Rectangle<int> screenRect)> onBlockClicked;

    /** Click a block on the chain bar to toggle its bypass state. */
    std::function<void (int blockId)> onBlockBypass;

    /** Right-click a block on the chain bar to open the editor of the
     *  plugin currently loaded in that block's category (or pop a chooser
     *  if multiple are loaded). */
    std::function<void (int blockId, juce::Rectangle<int> screenRect)> onBlockRightClicked;

private:
    SignalChainButton btnGate    { "GATE"  };
    SignalChainButton btnComp    { "COMP"  };
    SignalChainButton btnDrive   { "DRIVE" };
    SignalChainButton btnNam     { "NAM"   };
    SignalChainButton btnIr      { "IR"    };
    SignalChainButton btnEq      { "EQ"    };
    SignalChainButton btnMod     { "MOD"   };
    SignalChainButton btnDelay   { "DELAY" };
    SignalChainButton btnReverb  { "REVERB"};
    SignalChainButton btnLimiter { "LIMIT" };
    SignalChainButton btnFx      { "MASTER FX" };

    SignalChainButton* buttons[NumBlocks] {
        &btnGate, &btnComp, &btnDrive, &btnNam, &btnIr,
        &btnEq, &btnMod, &btnDelay, &btnReverb, &btnLimiter, &btnFx
    };

    bool modBeforeNam { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalChainBar)
};
