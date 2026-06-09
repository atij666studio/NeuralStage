#include "SignalChainBar.h"
#include "../Styles/Colours.h"
#include "../Styles/UIConstants.h"
#include "../Styles/AppLNF.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"
#include "../../Audio/NAM/NAMProcessor.h"

//==============================================================================
SignalChainButton::SignalChainButton (const juce::String& l)
    : juce::Button (l), label (l)
{
    setClickingTogglesState (false);
}

void SignalChainButton::mouseDown (const juce::MouseEvent& e)
{
    // Right-click / popup-modifier: open the editor of the loaded plugin in
    // this block -- DO NOT toggle bypass. Left-click falls through to the
    // base class (which fires onClick -> toggle bypass).
    if (e.mods.isPopupMenu())
    {
        if (onRightClick) onRightClick();
        return;
    }
    juce::Button::mouseDown (e);
}

void SignalChainButton::mouseUp (const juce::MouseEvent& e)
{
    // Swallow the mouseUp for right-clicks so the base class never fires
    // its click callback (which would toggle bypass on top of opening the
    // editor).
    if (e.mods.isPopupMenu() || e.mods.isRightButtonDown())
        return;

    if (dragging)
    {
        dragging = false;
        // Suppress the click that would otherwise fire on mouseUp.
        if (onDragEnd) onDragEnd (e.getScreenPosition());
        return;
    }
    juce::Button::mouseUp (e);
}

void SignalChainButton::mouseDrag (const juce::MouseEvent& e)
{
    if (draggable
        && ! e.mods.isPopupMenu()
        && e.getDistanceFromDragStart() > 6)
    {
        dragging = true;
    }
    juce::Button::mouseDrag (e);
}

//==============================================================================
namespace
{
    // Draws a small monochrome glyph that visually identifies a chain block.
    // `r` is the target square. Caller sets the colour before calling.
    void drawBlockIcon (juce::Graphics& g, juce::Rectangle<float> r, int blockId)
    {
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
        const float cx = r.getCentreX(), cy = r.getCentreY();
        const float stroke = juce::jmax (1.0f, juce::jmin (w, h) * 0.10f);

        switch (blockId)
        {
            case SignalChainBar::Gate:
            {
                // Audio waveform with a horizontal threshold line.
                juce::Path wave;
                const int steps = 18;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t  = (float) i / (float) steps;
                    const float px = x + t * w;
                    const float amp = (i % 4 == 0 ? 0.42f : 0.18f) * h * 0.5f;
                    const float py = cy + (i % 2 == 0 ? -amp : amp);
                    if (i == 0) wave.startNewSubPath (px, py);
                    else        wave.lineTo (px, py);
                }
                g.strokePath (wave, juce::PathStrokeType (stroke));
                g.drawLine (x, cy - h * 0.12f, x + w, cy - h * 0.12f, stroke * 0.7f);
                break;
            }
            case SignalChainBar::Comp:
            {
                // Compressor: descending stair.
                juce::Path p;
                p.startNewSubPath (x,             y + h);
                p.lineTo          (x + w * 0.45f, y + h);
                p.lineTo          (x + w * 0.45f, y + h * 0.55f);
                p.lineTo          (x + w * 0.80f, y + h * 0.55f);
                p.lineTo          (x + w * 0.80f, y + h * 0.20f);
                p.lineTo          (x + w,         y + h * 0.20f);
                g.strokePath (p, juce::PathStrokeType (stroke));
                break;
            }
            case SignalChainBar::Drive:
            {
                // Lightning bolt.
                juce::Path p;
                p.startNewSubPath (cx + w * 0.10f, y);
                p.lineTo          (cx - w * 0.20f, cy + h * 0.05f);
                p.lineTo          (cx + w * 0.05f, cy + h * 0.05f);
                p.lineTo          (cx - w * 0.15f, y + h);
                p.lineTo          (cx + w * 0.25f, cy - h * 0.05f);
                p.lineTo          (cx,             cy - h * 0.05f);
                p.closeSubPath();
                g.fillPath (p);
                break;
            }
            case SignalChainBar::NamAmp:
            {
                // Stylised tube glass.
                const float tw = w * 0.55f, th = h * 0.85f;
                auto tube = juce::Rectangle<float> (cx - tw * 0.5f, cy - th * 0.5f, tw, th);
                g.drawRoundedRectangle (tube, tw * 0.4f, stroke);
                g.fillEllipse (tube.withSizeKeepingCentre (tw * 0.35f, tw * 0.35f)
                                    .translated (0.0f, -th * 0.05f));
                g.drawLine (cx - tw * 0.2f, tube.getBottom() + stroke,
                             cx + tw * 0.2f, tube.getBottom() + stroke, stroke);
                break;
            }
            case SignalChainBar::IrCab:
            {
                // Speaker: outer rectangle + inner cone.
                auto cab = r.reduced (w * 0.12f, h * 0.06f);
                g.drawRoundedRectangle (cab, w * 0.08f, stroke);
                g.drawEllipse (cab.withSizeKeepingCentre (cab.getWidth() * 0.75f,
                                                          cab.getHeight() * 0.70f), stroke);
                g.fillEllipse (cab.withSizeKeepingCentre (cab.getWidth() * 0.28f,
                                                          cab.getHeight() * 0.28f));
                break;
            }
            case SignalChainBar::Eq:
            {
                // Five vertical bars of varying heights (peaks vs cuts).
                const float bw = w * 0.13f;
                const float gap = (w - bw * 5) / 6.0f;
                const float heights[5] = { 0.55f, 0.85f, 0.40f, 0.70f, 0.30f };
                for (int i = 0; i < 5; ++i)
                {
                    const float bh = h * heights[i];
                    const float bx = x + gap + i * (bw + gap);
                    g.fillRoundedRectangle (bx, y + h - bh, bw, bh, bw * 0.3f);
                }
                break;
            }
            case SignalChainBar::Mod:
            {
                // Smooth sine wave.
                juce::Path p;
                const int steps = 28;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t  = (float) i / (float) steps;
                    const float px = x + t * w;
                    const float py = cy + std::sin (t * juce::MathConstants<float>::twoPi) * h * 0.32f;
                    if (i == 0) p.startNewSubPath (px, py);
                    else        p.lineTo (px, py);
                }
                g.strokePath (p, juce::PathStrokeType (stroke));
                break;
            }
            case SignalChainBar::Delay:
            {
                // Three echo arcs.
                for (int i = 0; i < 3; ++i)
                {
                    const float a = h * (0.20f + 0.18f * i);
                    const float bw = w * (0.30f + 0.20f * i);
                    juce::Rectangle<float> e (cx - bw * 0.5f, cy + h * 0.10f - a * 0.5f, bw, a);
                    juce::Path arc;
                    arc.addCentredArc (e.getCentreX(), e.getCentreY(),
                                       e.getWidth() * 0.5f, e.getHeight() * 0.5f,
                                       0.0f,
                                       juce::MathConstants<float>::pi,
                                       juce::MathConstants<float>::twoPi,
                                       true);
                    g.strokePath (arc, juce::PathStrokeType (stroke));
                }
                break;
            }
            case SignalChainBar::Reverb:
            {
                // Concentric expanding semicircles + central dot.
                g.fillEllipse (cx - stroke, cy - stroke, stroke * 2.0f, stroke * 2.0f);
                for (int i = 1; i <= 3; ++i)
                {
                    const float rr = (float) i * juce::jmin (w, h) * 0.16f;
                    juce::Path arc;
                    arc.addCentredArc (cx, cy, rr, rr,
                                       0.0f,
                                       -juce::MathConstants<float>::halfPi,
                                       juce::MathConstants<float>::halfPi,
                                       true);
                    g.strokePath (arc, juce::PathStrokeType (stroke * 0.85f));
                }
                break;
            }
            case SignalChainBar::Limiter:
            {
                // Ceiling line + clipped tops.
                g.drawLine (x, y + h * 0.30f, x + w, y + h * 0.30f, stroke);
                juce::Path p;
                p.startNewSubPath (x,             y + h);
                p.lineTo          (x + w * 0.20f, y + h);
                p.lineTo          (x + w * 0.20f, y + h * 0.30f);
                p.lineTo          (x + w * 0.45f, y + h * 0.30f);
                p.lineTo          (x + w * 0.45f, y + h * 0.65f);
                p.lineTo          (x + w * 0.65f, y + h * 0.65f);
                p.lineTo          (x + w * 0.65f, y + h * 0.30f);
                p.lineTo          (x + w * 0.90f, y + h * 0.30f);
                p.lineTo          (x + w * 0.90f, y + h);
                p.lineTo          (x + w,         y + h);
                g.strokePath (p, juce::PathStrokeType (stroke * 0.85f));
                break;
            }
            case SignalChainBar::Fx:
            {
                // 4-pointed sparkle / star.
                juce::Path s;
                s.startNewSubPath (cx,             y);
                s.lineTo          (cx + w * 0.12f, cy - h * 0.12f);
                s.lineTo          (x + w,          cy);
                s.lineTo          (cx + w * 0.12f, cy + h * 0.12f);
                s.lineTo          (cx,             y + h);
                s.lineTo          (cx - w * 0.12f, cy + h * 0.12f);
                s.lineTo          (x,              cy);
                s.lineTo          (cx - w * 0.12f, cy - h * 0.12f);
                s.closeSubPath();
                g.fillPath (s);
                break;
            }
            default: break;
        }
    }
}

void SignalChainButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    juce::Colour bg = active ? ns::Colours::tealAccent : ns::Colours::chipUnsel;
    if (bypassed) bg = juce::Colour (0xFF2A2A33); // dim slate when bypassed
    if (isDown) bg = bg.brighter (0.10f);
    else if (isOver) bg = bg.brighter (0.05f);

    g.setColour (bg);
    g.fillRoundedRectangle (r, 8.0f);

    const auto textCol = bypassed ? juce::Colours::white.withAlpha (0.45f)
                                  : juce::Colours::white;
    g.setColour (textCol);

    if (loadedName.isNotEmpty())
    {
        // Two-line layout: category label small on top, plugin name larger
        // (or scaled-to-fit) on the bottom. Truncate to chip width with
        // ellipsis so even long plugin names stay readable.
        auto ri = r.toNearestInt();
        auto topRow    = ri.removeFromTop (ri.getHeight() / 2);
        auto bottomRow = ri;

        // Tiny icon left of the category label on the top row.
        if (blockId >= 0)
        {
            auto iconRect = juce::Rectangle<float> ((float) topRow.getX() + 3.0f,
                                                    (float) topRow.getY() + 2.0f,
                                                    (float) topRow.getHeight() - 4.0f,
                                                    (float) topRow.getHeight() - 4.0f);
            g.setColour (textCol.withAlpha (bypassed ? 0.40f : 0.85f));
            drawBlockIcon (g, iconRect, blockId);
            g.setColour (textCol);
        }

        g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
        g.drawFittedText (label, topRow, juce::Justification::centred, 1);

        // Strip trailing " (VST3)" / " (CLAP)" / " (AU)" so the chip just
        // shows the plugin's name. The format suffix is already visible in
        // the EDIT popup / loaded-plugins list.
        auto shortName = loadedName;
        const int parenIdx = shortName.lastIndexOf (" (");
        if (parenIdx > 0 && shortName.endsWithChar (')'))
            shortName = shortName.substring (0, parenIdx);

        g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
        g.drawFittedText (shortName, bottomRow.reduced (3, 0),
                          juce::Justification::centred, 1,
                          0.78f /* allow squeeze for long names */);
    }
    else
    {
        // Empty block (no plugin loaded): show big icon centred above the label.
        if (blockId >= 0)
        {
            auto ri = r.toNearestInt();
            auto iconArea = ri.removeFromTop ((int) (ri.getHeight() * 0.58f)).reduced (8, 4);
            const int sz = juce::jmin (iconArea.getWidth(), iconArea.getHeight());
            auto iconRect = juce::Rectangle<int> (iconArea.getCentreX() - sz / 2,
                                                  iconArea.getCentreY() - sz / 2,
                                                  sz, sz).toFloat();
            g.setColour (textCol.withAlpha (bypassed ? 0.40f : (active ? 0.95f : 0.78f)));
            drawBlockIcon (g, iconRect, blockId);
            g.setColour (textCol);
            g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle (active ? "Bold" : "")));
            g.drawFittedText (label, ri, juce::Justification::centred, 1);
        }
        else
        {
            g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle (active ? "Bold" : "")));
            g.drawFittedText (label, r.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    if (bypassed)
    {
        // Thin strike-through line so bypass is obvious even at a glance.
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        const float y = r.getCentreY();
        g.drawLine (r.getX() + 8.0f, y, r.getRight() - 8.0f, y, 1.2f);
    }

    if (badge > 0)
    {
        const float bd = 14.0f;
        auto b = juce::Rectangle<float> (r.getRight() - bd - 3.0f, r.getY() + 3.0f, bd, bd);
        g.setColour (juce::Colours::white);
        g.fillEllipse (b);
        g.setColour (ns::Colours::chipSel);
        g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
        g.drawFittedText (juce::String (badge), b.toNearestInt(),
                          juce::Justification::centred, 1);
    }
}

//==============================================================================
SignalChainBar::SignalChainBar()
{
    static const char* const tips[NumBlocks] = {
        "GATE -- add a noise gate / expander plugin.",
        "COMP -- add a compressor plugin.",
        "DRIVE -- add a drive / distortion / boost pedal.",
        "NAM -- load up to 4 NAM amp models and blend them.",
        "IR -- load any third-party IR loader plugin (Cab Lab, NadIR, MConvolutionEZ, etc.) for cab impulse responses.",
        "EQ -- add a parametric / graphic EQ plugin.",
        "MOD -- add a modulation effect (chorus, flanger, phaser).",
        "DELAY -- add a delay / echo plugin.",
        "REVERB -- add a reverb plugin.",
        "LIMIT -- add a brickwall / loudness limiter.",
        "MASTER FX -- final slot on the chain. Drop any plugin here (utilities, creative FX, master processors)."
    };
    for (int i = 0; i < NumBlocks; ++i)
    {
        addAndMakeVisible (buttons[i]);
        buttons[i]->setBlockId (i);
        buttons[i]->setTooltip (tips[i]);
        buttons[i]->onClick = [this, i]
        {
            // Single click on a chain block now toggles bypass for that block.
            // The right-edge EDIT button on the chain (see MainComponent)
            // owns the load/remove/replace popup that used to live here.
            if (onBlockBypass)
                onBlockBypass (i);
            else if (onBlockClicked)
                onBlockClicked (i, buttons[i]->getScreenBounds());
        };
        buttons[i]->onRightClick = [this, i]
        {
            // Right click -> open the editor for the plugin loaded in this
            // block (MainComponent does the chain lookup + chooser).
            if (onBlockRightClicked)
                onBlockRightClicked (i, buttons[i]->getScreenBounds());
        };
    }
    refreshBadges();

    // NAM left-click = bypass toggle (same behaviour as all other chain blocks;
    // the generic loop above already wired onClick = onBlockBypass).
    // Right-click = NAM settings overlay (via onBlockRightClicked, handled in
    // MainComponent: shows hosted plugin editor if loaded, else NamSettingsOverlay).

    // MASTER FX: left-click opens the plugin picker when the slot is empty.
    // If we let the bypass toggle fire on an empty slot it sets
    // categoryBypassOverride[Other]=true, and the next plugin loaded there
    // inherits bypassed=true -- so the button never lights up blue. When
    // a plugin IS loaded, left-click toggles bypass as normal.
    btnFx.onClick = [this]
    {
        if (btnFx.getBadge() == 0 && onBlockClicked)
            onBlockClicked (Fx, btnFx.getScreenBounds());
        else if (onBlockBypass)
            onBlockBypass (Fx);
    };

    // The MOD block is the only chip that's currently drag-repositionable.
    // The user can drag it across the bar -- releasing it left of NAM moves
    // it into the Pre-FX half (between DRIVE and NAM); releasing it right
    // of IR returns it to its default Post-FX position. Only affects which
    // chain newly-added modulation plugins are inserted into; existing
    // ones stay where they already are.
    btnMod.setDraggable (true);
    btnMod.setTooltip (juce::String ("MOD -- add a modulation effect (chorus, flanger, phaser).\n")
                       + "Drag this chip left (before NAM) to make new modulation plugins go into the Pre-FX chain, "
                       + "or drag it right (after IR) to return to the default Post-FX position.");
    btnMod.onDragEnd = [this] (juce::Point<int> screenPos)
    {
        const auto local = getLocalPoint (nullptr, screenPos);
        const int namCx  = btnNam.getBounds().getCentreX();
        setModBeforeNam (local.x < namCx);
    };
}

void SignalChainBar::setModBeforeNam (bool b)
{
    if (modBeforeNam == b) return;
    modBeforeNam = b;
    resized();
    if (onModPositionChanged) onModPositionChanged (b);
}

void SignalChainBar::refreshBadges()
{
    auto& eng = App::get().getAudioEngine();

    int counts[NumBlocks] = { 0 };
    juce::String firstName[NumBlocks]; // first loaded plugin per block

    auto bumpFromCategory = [&] (ns::FxCategory c, const juce::String& displayName)
    {
        auto bump = [&] (int blk)
        {
            if (counts[blk] == 0) firstName[blk] = displayName;
            counts[blk]++;
        };
        switch (c)
        {
            case ns::FxCategory::Gate:       bump (Gate);    break;
            case ns::FxCategory::Compressor: bump (Comp);    break;
            case ns::FxCategory::Drive:      bump (Drive);   break;
            case ns::FxCategory::EQ:         bump (Eq);      break;
            case ns::FxCategory::Modulation: bump (Mod);     break;
            case ns::FxCategory::Delay:      bump (Delay);   break;
            case ns::FxCategory::Reverb:     bump (Reverb);  break;
            case ns::FxCategory::Limiter:    bump (Limiter); break;
            case ns::FxCategory::IRLoader:   bump (IrCab);   break;
            case ns::FxCategory::Utility:
            case ns::FxCategory::Other:      bump (Fx);      break;
        }
    };

    auto preSlots  = eng.getPreFxChain().getSlotsForUI();
    auto postSlots = eng.getPostFxChain().getSlotsForUI();

    for (auto* s : preSlots)
    {
        if (s == nullptr || s->instance == nullptr) continue;
        // Use the stored slot category (which reflects the chain block the
        // user picked when loading the plugin), not classifyPlugin(d). A
        // Pro-Q 4 loaded via the MASTER FX block should count toward
        // MASTER FX, not get re-routed to the EQ block.
        bumpFromCategory (s->category, s->displayName);
    }
    for (auto* s : postSlots)
    {
        if (s == nullptr || s->instance == nullptr) continue;
        bumpFromCategory (s->category, s->displayName);
    }

    for (int i = 0; i < NumBlocks; ++i)
        buttons[i]->setBadgeCount (counts[i]);

    // Push the loaded-plugin display name into each chip. NAM and IR drive
    // their own labels below (NAM model name / IR loader plugin name).
    for (int i = 0; i < NumBlocks; ++i)
    {
        if (i == NamAmp) continue; // handled separately
        if (counts[i] == 0)        buttons[i]->setLoadedName ({});
        else if (counts[i] == 1)   buttons[i]->setLoadedName (firstName[i]);
        else                       buttons[i]->setLoadedName (juce::String (counts[i]) + " plugins");
    }

    // NAM chip: show the active NAM slot's model name when one is loaded.
    {
        auto& nam = eng.getNAM();
        if (nam.hasHostedPlugin())
        {
            // Hosted amp-sim plugin takes precedence over NAM models in the
            // audio path -- reflect that on the chip.
            btnNam.setLoadedName (nam.getHostedPluginName());
        }
        else if (nam.hasModel())
        {
            // Pick the first non-empty NAM slot (A/B/C/D) for the chip label.
            juce::String namName;
            for (int i = 0; i < 4; ++i)
                if (nam.hasSlot (i)) { namName = nam.getSlotName (i); break; }
            btnNam.setLoadedName (namName);
        }
        else
        {
            btnNam.setLoadedName ({});
        }
    }

    btnNam.setActive (eng.getNAM().hasModel() || eng.getNAM().hasHostedPlugin());
    // IR slot is now a plugin slot (any IR-loader plugin classified as
    // IRLoader category). Active when a plugin is present and the category
    // is not bypassed -- same convention as every other plugin block.
    btnIr.setActive (counts[IrCab] > 0
                     && ! eng.getPostFxChain().isCategoryBypassed (ns::FxCategory::IRLoader));

    // Reflect bypass state for each block from the live engine, so that a
    // MIDI footswitch / programmatic change is mirrored on the UI too.
    btnNam.setBypassed (eng.getNAM().isBypassed());
    btnIr .setBypassed (eng.getPostFxChain().isCategoryBypassed (ns::FxCategory::IRLoader));

    auto& pre  = eng.getPreFxChain();
    auto& post = eng.getPostFxChain();
    btnGate   .setBypassed (pre .isCategoryBypassed (ns::FxCategory::Gate));
    btnComp   .setBypassed (pre .isCategoryBypassed (ns::FxCategory::Compressor));
    btnDrive  .setBypassed (pre .isCategoryBypassed (ns::FxCategory::Drive));
    btnEq     .setBypassed (post.isCategoryBypassed (ns::FxCategory::EQ));
    btnMod    .setBypassed (post.isCategoryBypassed (ns::FxCategory::Modulation));
    btnDelay  .setBypassed (post.isCategoryBypassed (ns::FxCategory::Delay));
    btnReverb .setBypassed (post.isCategoryBypassed (ns::FxCategory::Reverb));
    btnLimiter.setBypassed (post.isCategoryBypassed (ns::FxCategory::Limiter));
    btnFx     .setBypassed (post.isCategoryBypassed (ns::FxCategory::Other)
                            || post.isCategoryBypassed (ns::FxCategory::Utility));

    // A block is "active" (lights up blue, like the selected SCENE button)
    // when it has at least one plugin / model loaded AND is not bypassed.
    // NAM / IR already drive their own active state from the engine above
    // (hasModel / isLoaded), so only override it when they are bypassed.
    for (int i = 0; i < NumBlocks; ++i)
    {
        const bool populated = (counts[i] > 0);
        const bool bypassed  = buttons[i]->isBypassed();
        if (i != NamAmp && i != IrCab)
            buttons[i]->setActive (populated && ! bypassed);
    }
    if (btnNam.isBypassed()) btnNam.setActive (false);
    if (btnIr .isBypassed()) btnIr .setActive (false);
}

void SignalChainBar::paint (juce::Graphics&) {}

void SignalChainBar::resized()
{
    using namespace ns::UI;
    const int n = NumBlocks;
    auto r = getLocalBounds().reduced (4, (getHeight() - kChainBlockH) / 2);
    const int totalW = n * kChainBlockW + (n - 1) * kChainSpacing;
    int x = r.getX() + (r.getWidth() - totalW) / 2;
    const int y = r.getY();

    // Default order = the buttons[] array (Gate, Comp, Drive, NAM, IR, EQ,
    // Mod, Delay, Reverb, Limit, Fx). When the user has dragged MOD into
    // the Pre-FX half, we slot it in between DRIVE and NAM instead.
    SignalChainButton* order[NumBlocks];
    if (! modBeforeNam)
    {
        for (int i = 0; i < NumBlocks; ++i) order[i] = buttons[i];
    }
    else
    {
        int w = 0;
        for (int i = 0; i < NumBlocks; ++i)
        {
            if (i == Mod) continue;     // skip -- insert it after DRIVE below
            if (i == NamAmp) order[w++] = &btnMod;
            order[w++] = buttons[i];
        }
    }

    for (int i = 0; i < n; ++i)
    {
        order[i]->setBounds (x, y, kChainBlockW, kChainBlockH);
        x += kChainBlockW + kChainSpacing;
    }
}
