#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../PluginHost/PluginCategory.h"
#include "../../PluginHost/PluginChain.h"

/** Popup shown when a signal-chain block (GATE / DRIVE / FX) is clicked.
 *  Top: filtered list of known plugins for this category + Scan button.
 *  Bottom: currently-loaded plugins in this slot's chain (Edit / Bypass / Remove / move).
 *
 *  Operates on the supplied PluginChain (preFxChain or postFxChain).
 */
class CategoryPopup : public juce::Component,
                      private juce::Timer
{
public:
    CategoryPopup (ns::FxCategory category,
                   PluginChain&   targetChain,
                   bool           showAllPlugins = false);
    ~CategoryPopup() override;

    void paint   (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onChainChanged;

    /** Open the plugin editor for the given slot in a centred top-level
     *  DocumentWindow. Used by both this popup and the EDIT-button menu in
     *  MainComponent so a user can jump straight from the chain to any
     *  loaded plugin's UI. */
    static void openPluginEditor (PluginSlot& slot);

private:
    void timerCallback() override;
    void rebuildPluginList();
    void rebuildChainList();
    void scanAll();
    void addSelected();
    void openEditor (int slotIndex);

    bool wasScanning { false };

    ns::FxCategory category;
    PluginChain&   chain;
    bool           showAll { false }; // MASTER FX shows every known plugin

    juce::Label    header;
    juce::Label    chainHeader { {}, "LOADED" };
    juce::TextButton scanBtn { "Rescan" };
    juce::TextButton addBtn  { "Add" };
    juce::TextButton resetBtn { "Reset Blacklist" };
    // Toggle that flips between category-filtered and full plugin list.
    // Label shows e.g. "GATE" when filtering by category and "ALL" when
    // showing every known plugin -- gives the user fast access to plugins
    // that classifyPlugin() may have miscategorised into a sibling slot.
    juce::TextButton filterToggleBtn { "ALL" };
    juce::TextEditor searchBox;
    juce::Label    status;
    juce::ListBox  pluginList;

    juce::Viewport       chainViewport;
    juce::Component      chainContent;
    juce::OwnedArray<juce::Component> chainRows;
    int                  lastChainCount { -1 };

    struct Model : public juce::ListBoxModel
    {
        CategoryPopup& owner;
        std::vector<juce::PluginDescription> rows;
        Model (CategoryPopup& o) : owner (o) {}
        int  getNumRows() override { return (int) rows.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    } model { *this };

    JUCE_DECLARE_WEAK_REFERENCEABLE (CategoryPopup)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CategoryPopup)
};
