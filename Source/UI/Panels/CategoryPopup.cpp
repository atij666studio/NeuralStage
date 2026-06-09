#include "CategoryPopup.h"
#include "../Styles/Colours.h"
#include "../Styles/Fonts.h"
#include "../Styles/AppWindowLNF.h"
#include "../Dialogs/ThemedAlerts.h"
#include "../../App.h"
#include "../../PluginHost/PluginManager.h"

namespace
{
    /** Single row used inside the popup's chain viewport. */
    class ChainRow : public juce::Component
    {
    public:
        ChainRow (int idx, const juce::String& name, bool bypassed)
        {
            nameLabel.setText (juce::String (idx + 1) + ". " + name, juce::dontSendNotification);
            nameLabel.setFont (ns::Fonts::value());
            nameLabel.setColour (juce::Label::textColourId, ns::Colours::textOnPanel);
            nameLabel.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (nameLabel);

            setMouseCursor (juce::MouseCursor::PointingHandCursor);

            bypassBtn.setToggleState (bypassed, juce::dontSendNotification);
            bypassBtn.setColour (juce::ToggleButton::textColourId, ns::Colours::textOnPanel);
            bypassBtn.setColour (juce::ToggleButton::tickColourId, ns::Colours::textOnPanel);
            bypassBtn.setColour (juce::ToggleButton::tickDisabledColourId, ns::Colours::textOnPanelDim);
            bypassBtn.onClick = [this] { if (onBypass) onBypass (bypassBtn.getToggleState()); };
            addAndMakeVisible (bypassBtn);

            auto style = [] (juce::TextButton& b)
            {
                b.setColour (juce::TextButton::buttonColourId,  ns::Colours::panelLight);
                // Button background is lavender; white text is unreadable.
                b.setColour (juce::TextButton::textColourOffId, ns::Colours::textOnPanel);
                b.setColour (juce::TextButton::textColourOnId,  ns::Colours::textOnPanel);
            };
            style (editBtn);   editBtn  .onClick = [this] { if (onEdit)   onEdit();   };
            style (removeBtn); removeBtn.onClick = [this] { if (onRemove) onRemove(); };
            style (upBtn);     upBtn    .onClick = [this] { if (onUp)     onUp();     };
            style (downBtn);   downBtn  .onClick = [this] { if (onDown)   onDown();   };

            addAndMakeVisible (editBtn);
            addAndMakeVisible (removeBtn);
            addAndMakeVisible (upBtn);
            addAndMakeVisible (downBtn);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (ns::Colours::panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 3);
            upBtn    .setBounds (r.removeFromRight (24).reduced (2));
            downBtn  .setBounds (r.removeFromRight (24).reduced (2));
            removeBtn.setBounds (r.removeFromRight (60).reduced (2));
            editBtn  .setBounds (r.removeFromRight (50).reduced (2));
            bypassBtn.setBounds (r.removeFromRight (76).reduced (2));
            nameLabel.setBounds (r);
            nameClickArea = r; // remember hit zone for mouseDown
        }

        // Single click anywhere on the name strip = open editor.
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (nameClickArea.contains (e.getPosition()) && onEdit)
                onEdit();
        }

        std::function<void(bool)> onBypass;
        std::function<void()>     onEdit, onRemove, onUp, onDown;

    private:
        juce::Label nameLabel;
        juce::Rectangle<int> nameClickArea;
        juce::ToggleButton bypassBtn { "Byp" };
        juce::TextButton editBtn   { "Edit" };
        juce::TextButton removeBtn { "Remove" };
        juce::TextButton upBtn     { "Up" };
        juce::TextButton downBtn   { "Dn" };
    };
}

CategoryPopup::CategoryPopup (ns::FxCategory cat, PluginChain& target, bool showAllPlugins)
    : category (cat), chain (target), showAll (showAllPlugins)
{
    setSize (520, 420);

    header.setText ((showAll ? juce::String ("MASTER FX (all plugins)")
                              : juce::String (ns::categoryName (cat)))
                    + " - pick a plugin",
                    juce::dontSendNotification);
    header.setFont (ns::Fonts::value());
    header.setColour (juce::Label::textColourId, ns::Colours::textPrimary);
    addAndMakeVisible (header);

    auto styleBtn = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,  ns::Colours::panelLight);
        // Lavender bg + white text is illegible -- use dark text instead.
        b.setColour (juce::TextButton::textColourOffId, ns::Colours::textOnPanel);
        b.setColour (juce::TextButton::textColourOnId,  ns::Colours::textOnPanel);
    };
    styleBtn (scanBtn); styleBtn (addBtn); styleBtn (resetBtn);
    scanBtn.onClick = [this] { scanAll(); };
    addBtn .onClick = [this] { addSelected(); };
    resetBtn.onClick = [this]
    {
        App::get().getPluginManager().clearBlacklist();
        status.setText ("Blacklist cleared. Run Scan again.", juce::dontSendNotification);
    };
    addAndMakeVisible (scanBtn);
    addAndMakeVisible (addBtn);
    addAndMakeVisible (resetBtn);

    // Filter toggle: "<CATEGORY>" when filtered, "ALL" when unfiltered.
    // MASTER FX is permanently in showAll mode -- the toggle is irrelevant
    // there so we hide it. For every other slot the toggle starts ON
    // (category filter active) and the label reflects the category name so
    // the user sees e.g. "GATE" highlighted -> clicking switches the label
    // to "ALL" and reveals every plugin (useful when a gate plugin was
    // mis-classified as Other / Utility and isn't showing up in the GATE
    // block).
    styleBtn (filterToggleBtn);
    filterToggleBtn.setClickingTogglesState (true);
    filterToggleBtn.setColour (juce::TextButton::buttonOnColourId,  ns::Colours::accent.withAlpha (0.55f));
    filterToggleBtn.setColour (juce::TextButton::textColourOnId,    ns::Colours::textPrimary);
    if (showAllPlugins)
    {
        // MASTER FX -- toggle is meaningless here, hide it.
        filterToggleBtn.setVisible (false);
    }
    else
    {
        filterToggleBtn.setToggleState (true, juce::dontSendNotification); // start category-filtered
        filterToggleBtn.setButtonText (juce::String (ns::categoryName (cat)));
        filterToggleBtn.setTooltip ("Filter by category. Click to show ALL plugins regardless of category "
                                    "(handy when a plugin was auto-classified into the wrong slot).");
        filterToggleBtn.onClick = [this]
        {
            const bool filtered = filterToggleBtn.getToggleState();
            showAll = ! filtered;
            filterToggleBtn.setButtonText (filtered ? juce::String (ns::categoryName (category))
                                                     : juce::String ("ALL"));
            header.setText ((showAll ? juce::String (ns::categoryName (category)) + " (showing ALL)"
                                      : juce::String (ns::categoryName (category)))
                            + " - pick a plugin",
                            juce::dontSendNotification);
            rebuildPluginList();
        };
        addAndMakeVisible (filterToggleBtn);
    }

    pluginList.setModel (&model);
    pluginList.setColour (juce::ListBox::backgroundColourId, ns::Colours::panel);
    pluginList.setRowHeight (22);
    addAndMakeVisible (pluginList);

    // Reaper-style live filter. Typing narrows the list as you go; pressing
    // Enter or double-clicking adds the highlighted row. Escape clears the
    // filter.
    searchBox.setTextToShowWhenEmpty ("Search plugins...",
                                      ns::Colours::textOnPanelDim);
    searchBox.setColour (juce::TextEditor::backgroundColourId, ns::Colours::panel);
    searchBox.setColour (juce::TextEditor::textColourId,       ns::Colours::textOnPanel);
    searchBox.setColour (juce::TextEditor::highlightColourId,  ns::Colours::accent.withAlpha (0.35f));
    searchBox.setColour (juce::TextEditor::outlineColourId,    ns::Colours::panelLight);
    searchBox.onTextChange = [this] { rebuildPluginList(); };
    searchBox.onReturnKey  = [this]
    {
        if (model.rows.empty()) return;
        if (pluginList.getSelectedRow() < 0) pluginList.selectRow (0);
        addSelected();
    };
    searchBox.onEscapeKey  = [this]
    {
        searchBox.clear();
        rebuildPluginList();
    };
    addAndMakeVisible (searchBox);

    status.setFont (ns::Fonts::small());
    status.setColour (juce::Label::textColourId, ns::Colours::textSecondary);
    addAndMakeVisible (status);

    chainHeader.setFont (ns::Fonts::sectionLabel());
    chainHeader.setColour (juce::Label::textColourId, ns::Colours::textSecondary);
    addAndMakeVisible (chainHeader);

    chainViewport.setViewedComponent (&chainContent, false);
    chainViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (chainViewport);

    rebuildPluginList();
    rebuildChainList();
    startTimerHz (4);
}

CategoryPopup::~CategoryPopup()
{
    masterReference.clear();
}

void CategoryPopup::timerCallback()
{
    // Disable Rescan while a global scan is in flight.
    const bool scanning = App::get().getPluginManager().isScanning();
    scanBtn.setEnabled (! scanning);

    if (chain.getNumSlots() != lastChainCount) rebuildChainList();

    // Auto-refresh our list when a running scan completes.
    if (wasScanning && ! scanning)
        rebuildPluginList();
    wasScanning = scanning;
}

void CategoryPopup::rebuildPluginList()
{
    auto& known = App::get().getPluginManager().getKnownList();
    const auto filter = searchBox.getText().trim();
    const bool hasFilter = filter.isNotEmpty();

    model.rows.clear();
    for (auto& d : known.getTypes())
    {
        // MASTER FX (showAll) bypasses category filtering — the user wants
        // every known plugin, including Drive / EQ / Reverb etc. that would
        // otherwise be hidden by classifyPlugin().
        if (! showAll)
        {
            const auto c = ns::classifyPlugin (d);
            const bool match = (category == ns::FxCategory::Other)
                                  ? (c == ns::FxCategory::Other || c == ns::FxCategory::Utility)
                                  : (c == category);
            if (! match) continue;
        }
        if (hasFilter
            && ! d.name.containsIgnoreCase (filter)
            && ! d.manufacturerName.containsIgnoreCase (filter))
            continue;
        model.rows.push_back (d);
    }

    std::sort (model.rows.begin(), model.rows.end(),
               [] (const auto& a, const auto& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });

    pluginList.updateContent();
    pluginList.deselectAllRows();
    if (hasFilter && ! model.rows.empty())
        pluginList.selectRow (0);

    const juce::String count = juce::String (model.rows.size())
                             + (hasFilter ? juce::String (" match") + (model.rows.size() == 1 ? "" : "es")
                                          : (showAll ? juce::String (" plugins known")
                                                     : juce::String (" ") + ns::categoryName (category) + " plugins known"));
    status.setText (count + ".", juce::dontSendNotification);
}

void CategoryPopup::scanAll()
{
    status.setText ("Scanning... (this may take a minute)", juce::dontSendNotification);
    scanBtn.setEnabled (false);

    juce::WeakReference<CategoryPopup> weak (this);
    auto& mgr = App::get().getPluginManager();

    mgr.beginAsyncScan (
        [weak] (const juce::String& current)
        {
            if (auto* self = weak.get())
                self->status.setText ("Scanning: " + current, juce::dontSendNotification);
        },
        [weak]
        {
            if (auto* self = weak.get())
            {
                self->scanBtn.setEnabled (true);
                self->rebuildPluginList();
                self->status.setText ("Scan complete.", juce::dontSendNotification);
            }
        });
}

void CategoryPopup::addSelected()
{
    const int row = pluginList.getSelectedRow();
    if (row < 0 || row >= (int) model.rows.size()) return;

    auto desc = model.rows[(size_t) row];
    auto& eng = App::get().getAudioEngine();
    auto& mgr = App::get().getPluginManager();

    // NOTE: NO pre-flight validateBeforeAdd here. The plugin is in the
    // picker list, which means the scanner already validated it in a
    // child process. Re-validating wastes 30-60 s per add for heavy
    // plugins (Neural DSP Archetype, Waves, etc.) — Reaper doesn't do
    // this either. Crash safety still comes from GuardedAdd: if
    // instantiation crashes the host, PendingAdd.txt is on disk and
    // handleCrashedAddOnLaunch will blacklist it on next launch.

    // Disable the Add button + show progress; loading large NN-based
    // plugins (Archetype, Tonex etc.) can legitimately take 30+ s.
    addBtn.setEnabled (false);
    status.setText ("Loading " + desc.name + "...", juce::dontSendNotification);

    // Begin the guarded-add sentinel BEFORE launching the async load so
    // a crash during instantiation is captured on next launch.
    mgr.beginGuardedAdd (desc.fileOrIdentifier);

    juce::WeakReference<CategoryPopup> weak (this);
    const double sr = eng.getCurrentSampleRate();
    const int    bs = eng.getCurrentBlockSize();

    mgr.getFormats().createPluginInstanceAsync (desc, sr, bs,
        [weak, desc] (std::unique_ptr<juce::AudioPluginInstance> inst,
                      const juce::String& err)
    {
        // Always close the guarded-add sentinel on the message thread.
        App::get().getPluginManager().endGuardedAdd();

        auto* self = weak.get();
        if (self == nullptr)
        {
            // Popup dismissed mid-load. The instance, if created, is
            // released here automatically — no leak, no chain insert.
            return;
        }

        self->addBtn.setEnabled (true);

        if (inst == nullptr)
        {
            self->status.setText ("Add failed: " + err, juce::dontSendNotification);
            const auto pluginName = desc.name;
            const auto reason     = err.isNotEmpty() ? err : juce::String ("Unknown error");
            juce::MessageManager::callAsync ([pluginName, reason]
            {
                ns::ThemedAlerts::showWarning (
                    "Plugin could not be loaded",
                    "\"" + pluginName + "\"\n\n" + reason);
            });
            return;
        }

        self->chain.addPlugin (std::move (inst),
                               desc.name + " (" + desc.pluginFormatName + ")",
                               desc.createIdentifierString(),
                               self->category);
        self->status.setText ("Added: " + desc.name, juce::dontSendNotification);
        self->rebuildChainList();
        if (self->onChainChanged) self->onChainChanged();
    });
}

void CategoryPopup::openEditor (int slotIndex)
{
    if (auto* slot = chain.getSlotForUI (slotIndex))
        openPluginEditor (*slot);
}

void CategoryPopup::openPluginEditor (PluginSlot& slot)
{
    if (slot.instance == nullptr) return;
    if (slot.editorWindow != nullptr) { slot.editorWindow->toFront (true); return; }

    auto* editor = slot.instance->createEditorIfNeeded();
    if (editor == nullptr) return;

    struct EditorWindow : public juce::DocumentWindow
    {
        EditorWindow (const juce::String& title, PluginSlot& s)
            : juce::DocumentWindow (title,
                                    ns::Colours::background,
                                    juce::DocumentWindow::closeButton),
              slotRef (s)
        {
            setLookAndFeel (&ns::appWindowLNF());
        }
        ~EditorWindow() override { setLookAndFeel (nullptr); }

        void closeButtonPressed() override
        {
            // Clear the editor content + drop the unique_ptr asynchronously
            // so we don't delete this window from inside its own button
            // callback chain.
            setVisible (false);
            clearContentComponent();
            juce::MessageManager::callAsync ([slot = &slotRef]
            {
                if (slot != nullptr && slot->editorWindow != nullptr)
                    slot->editorWindow.reset();
            });
        }

        PluginSlot& slotRef;
    };

    auto win = std::make_unique<EditorWindow> (slot.displayName, slot);
    win->setUsingNativeTitleBar (false);
    win->setContentOwned (editor, true);
    win->setResizable (editor->isResizable(), false);
    // Keep the plugin editor above the main app so clicking a Scene button
    // (or anywhere on the main window) doesn't bury it.
    win->setAlwaysOnTop (true);

    // Centre over the main app window (standalone or DAW plugin host), not
    // the screen, so the editor always appears on top of the rig UI. Skip
    // any top-level that is itself a dialog or a plugin editor window so a
    // previously-opened editor never becomes the new editor's anchor.
    const int w = editor->getWidth();
    const int h = editor->getHeight();
    juce::Component* anchor = nullptr;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
    {
        auto* tw = juce::TopLevelWindow::getTopLevelWindow (i);
        if (tw == nullptr) continue;
        if (dynamic_cast<juce::DialogWindow*> (tw) != nullptr) continue;
        if (dynamic_cast<EditorWindow*>      (tw) != nullptr) continue;
        anchor = tw;
        break;
    }
    if (anchor != nullptr) win->centreAroundComponent (anchor, w, h);
    else                   win->centreWithSize (w, h);

    win->setVisible (true);
    slot.editorWindow = std::move (win);
}

void CategoryPopup::rebuildChainList()
{
    chainRows.clear();
    auto slots = chain.getSlotsForUI();
    lastChainCount = (int) slots.size();

    for (int i = 0; i < (int) slots.size(); ++i)
    {
        auto* s = slots[(size_t) i];
        auto* row = new ChainRow (i, s->displayName, s->bypassed.load());
        row->onBypass = [this, i] (bool b) { chain.setBypassed (i, b); };
        row->onEdit   = [this, i] { openEditor (i); };
        row->onRemove = [this, i] { chain.removePlugin (i); rebuildChainList();
                                    if (onChainChanged) onChainChanged(); };
        row->onUp     = [this, i] { chain.moveSlot (i, i - 1); rebuildChainList(); };
        row->onDown   = [this, i] { chain.moveSlot (i, i + 1); rebuildChainList(); };
        chainContent.addAndMakeVisible (row);
        chainRows.add (row);
    }
    resized();
}

void CategoryPopup::paint (juce::Graphics& g)
{
    g.fillAll (ns::Colours::background);
}

void CategoryPopup::resized()
{
    auto r = getLocalBounds().reduced (10);

    header.setBounds (r.removeFromTop (24));

    auto btnRow = r.removeFromTop (28);
    scanBtn .setBounds (btnRow.removeFromLeft (80).reduced (1));
    addBtn  .setBounds (btnRow.removeFromLeft (60).reduced (1));
    resetBtn.setBounds (btnRow.removeFromLeft (130).reduced (1));
    if (filterToggleBtn.isVisible())
        filterToggleBtn.setBounds (btnRow.removeFromLeft (110).reduced (1));

    // Search row sits between the action buttons and the list, full width.
    r.removeFromTop (4);
    searchBox.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);

    auto listArea = r.removeFromTop (180);
    pluginList.setBounds (listArea);

    status.setBounds (r.removeFromTop (18));

    chainHeader.setBounds (r.removeFromTop (20));

    chainViewport.setBounds (r);
    const int rowH = 30;
    chainContent.setSize (chainViewport.getWidth() - 12,
                          juce::jmax (rowH, chainRows.size() * (rowH + 4)));
    for (int i = 0; i < chainRows.size(); ++i)
        chainRows[i]->setBounds (0, i * (rowH + 4), chainContent.getWidth(), rowH);
}

//==============================================================================
void CategoryPopup::Model::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    auto& d = rows[(size_t) row];

    if (selected) g.fillAll (ns::Colours::accent.withAlpha (0.35f));
    // List background is lavender, so use dark text for legibility.
    g.setColour (juce::Colours::black);
    g.setFont (ns::Fonts::value());
    g.drawText (d.name, 8, 0, w - 80, h, juce::Justification::centredLeft);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.setFont (ns::Fonts::small());
    g.drawText (d.pluginFormatName, w - 76, 0, 68, h, juce::Justification::centredRight);
}

void CategoryPopup::Model::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    owner.pluginList.selectRow (row);
    owner.addSelected();
}
