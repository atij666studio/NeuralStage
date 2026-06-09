#include "Dialogs.h"
#include "ThemedAlerts.h"
#include "../Styles/Colours.h"
#include "../Styles/AppWindowLNF.h"
#include "../../App.h"
#include "../../Audio/AudioEngine.h"
#include "../../Core/PresetManager.h"
#include "../../MIDI/MIDILearn.h"
#include "../../Utils/FileUtils.h"
#include "BinaryData.h"
#include <map>
#include <set>

#ifndef JUCE_APPLICATION_VERSION_STRING
 #define JUCE_APPLICATION_VERSION_STRING "0.2.0"
#endif

namespace ns::Dialogs
{
namespace
{
    /** Pick the main app window (standalone host or DAW plugin window) so
     *  dialog popups appear centred over the rig UI instead of the screen. */
    juce::Component* getAppMainWindow()
    {
        // Prefer the currently active top-level window, but skip dialog/
        // document windows that are themselves popups so the parent rig UI
        // wins.
        for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        {
            auto* w = juce::TopLevelWindow::getTopLevelWindow (i);
            if (w == nullptr) continue;
            if (dynamic_cast<juce::DialogWindow*>   (w) != nullptr) continue;
            if (dynamic_cast<juce::DocumentWindow*> (w) != nullptr
                && w->getName().containsIgnoreCase ("editor")) continue;
            return w;
        }
        return juce::TopLevelWindow::getActiveTopLevelWindow();
    }
}
//==============================================================================
// Audio / MIDI settings
//==============================================================================
namespace
{
    /** AudioDeviceSelector + Mute Input / Mute Output toggle row across the top.
     *  The mute row is the first thing the user sees, so they can silence the
     *  signal path BEFORE hot-plugging interfaces, switching sample rates or
     *  enabling MIDI inputs (any of which can produce a feedback burst).
     */
    class AudioMidiSettings : public juce::Component
    {
    public:
        AudioMidiSettings()
        {
            auto& dm = App::get().getAudioEngine().getDeviceManager();
            sel = std::make_unique<juce::AudioDeviceSelectorComponent> (
                dm,
                /*minIn*/  0, /*maxIn*/  2,
                /*minOut*/ 1, /*maxOut*/ 2,
                /*showMidiIn*/ true, /*showMidiOut*/ false,
                /*stereoChannelPairs*/ true, /*hideAdvanced*/ false);
            addAndMakeVisible (*sel);

            auto styleMute = [] (juce::TextButton& b)
            {
                b.setClickingTogglesState (true);
                b.setColour (juce::TextButton::buttonColourId,   ns::Colours::chipUnsel);
                b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd03a3a));
                b.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
                b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            };
            styleMute (muteIn);
            styleMute (muteOut);
            muteIn .setButtonText ("MUTE INPUT");
            muteOut.setButtonText ("MUTE OUTPUT");
            muteIn .setTooltip ("Mute the guitar input (use before hot-plugging or changing sample rate)");
            muteOut.setTooltip ("Mute the master output (use before changing the audio device to avoid feedback)");

            auto& eng = App::get().getAudioEngine();
            muteIn .setToggleState (eng.getInput().isMuted(),  juce::dontSendNotification);
            muteOut.setToggleState (eng.getOutput().isMuted(), juce::dontSendNotification);
            muteIn .onClick = [this] { App::get().getAudioEngine().getInput() .setMute (muteIn .getToggleState()); };
            muteOut.onClick = [this] { App::get().getAudioEngine().getOutput().setMute (muteOut.getToggleState()); };

            addAndMakeVisible (muteIn);
            addAndMakeVisible (muteOut);

            setSize (520, 580);
        }

        ~AudioMidiSettings() override
        {
            // The AudioDeviceSelectorComponent (showMidiIn = true) takes over
            // MIDI-input enablement while this dialog is open and can leave our
            // MIDIManager callback detached / inputs disabled. Re-assert them on
            // close so MIDI Learn and footswitches keep receiving (this also
            // picks up any controller hot-plugged while the dialog was open).
            App::get().getMIDIManager().refresh (
                App::get().getAudioEngine().getDeviceManager());
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            auto top = r.removeFromTop (28);
            const int gap = 6;
            const int bw  = (top.getWidth() - gap) / 2;
            muteIn .setBounds (top.removeFromLeft (bw));
            top.removeFromLeft (gap);
            muteOut.setBounds (top);
            r.removeFromTop (8);
            sel->setBounds (r);
        }

    private:
        std::unique_ptr<juce::AudioDeviceSelectorComponent> sel;
        juce::TextButton muteIn, muteOut;
    };
}

void showAudioMidiSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle                  = "Audio / MIDI Settings";
    o.dialogBackgroundColour       = ns::Colours::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar            = false;
    o.resizable                    = false;
    o.componentToCentreAround      = getAppMainWindow();
    o.content.setOwned (new AudioMidiSettings());
    if (auto* dw = o.launchAsync())
        dw->setLookAndFeel (&ns::appWindowLNF());
}

//==============================================================================
// Preset browser
//==============================================================================
namespace
{
    // The preset browser is shown non-modally (so the scene buttons and the
    // rest of the rig stay clickable while it floats on top). We keep a single
    // owning pointer so re-clicking PRESETS just brings the existing window to
    // front instead of stacking duplicates, and so the Close button / title-bar
    // X / Escape can all tear it down cleanly.
    std::unique_ptr<juce::DialogWindow> gPresetWindow;

    class PresetBrowser : public juce::Component,
                          private juce::ListBoxModel
    {
    public:
        PresetBrowser()
        {
            setSize (560, 460);
            list.setModel (this);
            list.setColour (juce::ListBox::backgroundColourId, ns::Colours::panel);
            list.setRowHeight (34); // taller for two-line (name + tags)
            addAndMakeVisible (list);

            // Category filter -- populated from on-disk presets every refresh().
            addAndMakeVisible (categoryFilter);
            categoryFilter.setTextWhenNothingSelected ("All categories");
            categoryFilter.onChange = [this] { refresh(); };

            addAndMakeVisible (editTagsBtn);
            editTagsBtn.setButtonText ("Edit Tags...");
            editTagsBtn.onClick = [this] { editTagsForCurrent(); };

            for (auto* b : { &saveBtn, &saveAsBtn, &loadBtn, &delBtn, &openFolderBtn, &closeBtn })
                addAndMakeVisible (b);

            saveBtn      .setButtonText ("Save");
            saveAsBtn    .setButtonText ("Save As...");
            loadBtn      .setButtonText ("Load");
            delBtn       .setButtonText ("Delete");
            openFolderBtn.setButtonText ("Show Folder");
            closeBtn     .setButtonText ("Close");

            saveBtn.onClick = [this]
            {
                if (currentFile.existsAsFile())
                {
                    App::get().pushUndoSnapshot();
                    App::get().getPresetManager().save (currentFile);
                }
                else saveAsBtn.triggerClick();
            };

            saveAsBtn.onClick = [this]
            {
                auto dir = App::get().getPresetManager().presetsDirectory();
                fc = std::make_unique<juce::FileChooser> ("Save Preset", dir, "*.nspreset");
                fc->launchAsync (juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this] (const juce::FileChooser& c)
                    {
                        auto f = c.getResult();
                        if (f == juce::File{}) return;
                        if (! f.hasFileExtension ("nspreset"))
                            f = f.withFileExtension ("nspreset");
                        App::get().getPresetManager().save (f);
                        currentFile = f;
                        refresh();
                    });
            };

            loadBtn.onClick = [this]
            {
                if (currentFile.existsAsFile())
                {
                    App::get().pushUndoSnapshot();
                    App::get().getPresetManager().load (currentFile);
                }
            };

            delBtn.onClick = [this]
            {
                if (! currentFile.existsAsFile()) return;
                currentFile.deleteFile();
                currentFile = juce::File{};
                refresh();
            };

            openFolderBtn.onClick = []
            {
                App::get().getPresetManager().presetsDirectory().revealToUser();
            };

            closeBtn.onClick = []
            {
                // Non-modal window: defer teardown to the message loop so we
                // never delete the window from inside its own child's click.
                juce::MessageManager::callAsync ([] { gPresetWindow.reset(); });
            };

            refresh();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            // Top row: category filter + Edit Tags button.
            auto top = r.removeFromTop (26);
            categoryFilter.setBounds (top.removeFromLeft (220));
            top.removeFromLeft (8);
            editTagsBtn.setBounds (top.removeFromLeft (110));
            r.removeFromTop (6);
            auto buttons = r.removeFromBottom (28);
            const int n = 6;
            const int bw = (buttons.getWidth() - 5 * 6) / n;
            for (auto* b : { &saveBtn, &saveAsBtn, &loadBtn, &delBtn, &openFolderBtn, &closeBtn })
            {
                b->setBounds (buttons.removeFromLeft (bw));
                buttons.removeFromLeft (6);
            }
            r.removeFromBottom (6);
            list.setBounds (r);
        }

        // ListBoxModel
        int getNumRows() override { return filtered.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, filtered.size())) return;
            const auto& f = filtered.getReference (row);
            g.fillAll (selected ? ns::Colours::tealAccent.withAlpha (0.45f)
                                : juce::Colours::transparentBlack);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (f.getFileNameWithoutExtension(),
                        8, 2, w - 16, 16, juce::Justification::centredLeft);

            // Second line: category + tags (cached during refresh).
            const auto it = metaCache.find (f.getFullPathName());
            if (it != metaCache.end())
            {
                juce::String sub;
                if (it->second.category.isNotEmpty())
                    sub << "[" << it->second.category << "] ";
                sub << it->second.tags;
                g.setColour (ns::Colours::textSecondary);
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawText (sub, 8, 18, w - 16, 14, juce::Justification::centredLeft);
            }
        }

        void selectedRowsChanged (int row) override
        {
            currentFile = juce::isPositiveAndBelow (row, filtered.size())
                          ? filtered.getReference (row) : juce::File{};
        }

        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            if (juce::isPositiveAndBelow (row, filtered.size()))
            {
                currentFile = filtered.getReference (row);
                App::get().pushUndoSnapshot();
                App::get().getPresetManager().load (currentFile);
            }
        }

    private:
        struct Meta { juce::String category, tags; };

        void editTagsForCurrent()
        {
            if (! currentFile.existsAsFile()) return;
            auto cat  = PresetManager::readCategoryFromFile (currentFile);
            auto tags = PresetManager::readTagsFromFile     (currentFile);

            // Two-step themed input: category first, then tags. Cancel at
            // either step aborts cleanly without touching the file.
            juce::Component::SafePointer<PresetBrowser> safe (this);
            const auto file = currentFile;

            ns::ThemedAlerts::showTextInput (
                "Edit Tags -- " + file.getFileNameWithoutExtension(),
                "Category (e.g. Clean, Crunch, Lead):",
                cat,
                [safe, file, tags] (juce::String newCat)
                {
                    if (newCat.isEmpty() && safe.getComponent() == nullptr) return;
                    ns::ThemedAlerts::showTextInput (
                        "Edit Tags -- " + file.getFileNameWithoutExtension(),
                        "Tags (comma-separated):",
                        tags,
                        [safe, file, newCat] (juce::String newTags)
                        {
                            PresetManager::writeMetaToFile (file, newCat, newTags);
                            if (auto* p = safe.getComponent()) p->refresh();
                        });
                });
        }

        void refresh()
        {
            files = App::get().getPresetManager().listPresets();

            // Rebuild metadata cache + category list.
            metaCache.clear();
            std::set<juce::String> cats;
            for (auto& f : files)
            {
                Meta m;
                m.category = PresetManager::readCategoryFromFile (f);
                m.tags     = PresetManager::readTagsFromFile     (f);
                metaCache[f.getFullPathName()] = m;
                if (m.category.isNotEmpty()) cats.insert (m.category);
            }

            // Rebuild category combo while preserving selection.
            const auto prevSel = categoryFilter.getText();
            categoryFilter.clear (juce::dontSendNotification);
            categoryFilter.addItem ("All categories", 1);
            categoryFilter.addItem ("(Uncategorised)", 2);
            int id = 3;
            for (auto& c : cats) categoryFilter.addItem (c, id++);
            // Reselect previous (or default to "All").
            bool restored = false;
            for (int i = 0; i < categoryFilter.getNumItems(); ++i)
                if (categoryFilter.getItemText (i) == prevSel)
                    { categoryFilter.setSelectedItemIndex (i, juce::dontSendNotification); restored = true; break; }
            if (! restored) categoryFilter.setSelectedId (1, juce::dontSendNotification);

            // Apply filter.
            filtered.clearQuick();
            const auto sel = categoryFilter.getText();
            for (auto& f : files)
            {
                const auto& m = metaCache[f.getFullPathName()];
                if (sel == "All categories"
                    || (sel == "(Uncategorised)" && m.category.isEmpty())
                    || sel == m.category)
                    filtered.add (f);
            }

            list.updateContent();
            list.repaint();
        }

        juce::ListBox            list;
        juce::Array<juce::File>  files;     // all on disk
        juce::Array<juce::File>  filtered;  // after category filter
        std::map<juce::String, Meta> metaCache;
        juce::ComboBox           categoryFilter;
        juce::File               currentFile;
        juce::TextButton         editTagsBtn;
        juce::TextButton         saveBtn, saveAsBtn, loadBtn, delBtn, openFolderBtn, closeBtn;
        std::unique_ptr<juce::FileChooser> fc;
    };
}

void showPresetBrowser()
{
    // Already open -> just raise it.
    if (gPresetWindow != nullptr)
    {
        gPresetWindow->toFront (true);
        return;
    }

    struct PresetWindow : public juce::DialogWindow
    {
        PresetWindow()
            : juce::DialogWindow ("Presets", ns::Colours::background,
                                  /*escapeKeyTriggersClose*/ true) {}
        // Title-bar X and Escape both route here; tear down the single owner.
        void closeButtonPressed() override { gPresetWindow.reset(); }
    };

    auto* w = new PresetWindow();
    w->setUsingNativeTitleBar (false);
    w->setContentOwned (new PresetBrowser(), true);
    w->setLookAndFeel (&ns::appWindowLNF());
    w->setResizable (false, false);
    w->setAlwaysOnTop (true); // float above the rig so it stays in view
    if (auto* parent = getAppMainWindow())
        w->centreAroundComponent (parent, w->getWidth(), w->getHeight());
    else
        w->centreWithSize (w->getWidth(), w->getHeight());
    w->setVisible (true);
    gPresetWindow.reset (w);
}

//==============================================================================
// MIDI assignments table
//==============================================================================
namespace
{
    class MidiAssignTable : public juce::Component,
                            private juce::TableListBoxModel
    {
    public:
        MidiAssignTable()
        {
            setSize (520, 388);
            addAndMakeVisible (table);
            table.setColour (juce::ListBox::backgroundColourId, ns::Colours::panel);
            table.setRowHeight (22);
            table.getHeader().addColumn ("Parameter", 1, 220);
            table.getHeader().addColumn ("Channel",   2,  70);
            table.getHeader().addColumn ("Type",      3,  70);
            table.getHeader().addColumn ("CC / Note", 4, 100);
            table.setModel (this);

            lastMidiLabel.setText ("Listening... (move a pedal / knob to verify input)",
                                   juce::dontSendNotification);
            lastMidiLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
            lastMidiLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
            lastMidiLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (lastMidiLabel);

            clearAllBtn.setButtonText ("Clear All");
            clearAllBtn.onClick = []
            {
                App::get().getMIDILearn().clearAll();
            };
            addBtn.setButtonText ("Add...");
            addBtn.onClick = [this] { showAddEditDialog (juce::String()); };
            editBtn.setButtonText ("Edit");
            editBtn.onClick = [this]
            {
                if (auto sel = table.getSelectedRow();
                    juce::isPositiveAndBelow (sel, mappings.size()))
                    showAddEditDialog (mappings.getReference (sel).paramId);
            };
            learnBtn.setButtonText ("Learn");
            learnBtn.onClick = [this]
            {
                if (auto sel = table.getSelectedRow();
                    juce::isPositiveAndBelow (sel, mappings.size()))
                    App::get().getMIDILearn().beginLearn (mappings.getReference (sel).paramId);
            };
            closeBtn.setButtonText ("Close");
            closeBtn.onClick = [this]
            {
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (0);
            };
            addAndMakeVisible (addBtn);
            addAndMakeVisible (editBtn);
            addAndMakeVisible (learnBtn);
            addAndMakeVisible (clearAllBtn);
            addAndMakeVisible (closeBtn);

            App::get().getMIDILearn().onChanged = [this] { refresh(); };

            // Live MIDI input echo so user can verify their controller is sending
            // and our app is receiving (including PC messages). Audio thread ->
            // message thread.
            App::get().getMIDIManager().onMessage = [safe = juce::Component::SafePointer<MidiAssignTable> (this)]
                                                    (const juce::MidiMessage& m)
            {
                juce::String desc;
                if      (m.isController())     desc = "CC "   + juce::String (m.getControllerNumber())
                                                    + "   value " + juce::String (m.getControllerValue());
                else if (m.isNoteOn())         desc = "Note " + juce::String (m.getNoteNumber())
                                                    + "   vel "   + juce::String ((int) m.getVelocity());
                else if (m.isProgramChange())  desc = "PC "   + juce::String (m.getProgramChangeNumber());
                else                            return;
                const int ch = m.getChannel();
                const auto chStr = (ch >= 1 && ch <= 16) ? "   ch " + juce::String (ch) : juce::String();
                juce::MessageManager::callAsync ([safe, desc, chStr]
                {
                    if (auto* self = safe.getComponent())
                    {
                        self->lastMidiLabel.setText ("Last in:  " + desc + chStr,
                                                     juce::dontSendNotification);
                        self->lastMidiLabel.setColour (juce::Label::textColourId,
                                                       ns::Colours::accentGlow);
                    }
                });
            };
            refresh();
        }

        ~MidiAssignTable() override
        {
            App::get().getMIDILearn().onChanged   = nullptr;
            App::get().getMIDIManager().onMessage = nullptr;
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            auto buttons = r.removeFromBottom (28);
            addBtn     .setBounds (buttons.removeFromLeft (60));  buttons.removeFromLeft (4);
            editBtn    .setBounds (buttons.removeFromLeft (60));  buttons.removeFromLeft (4);
            learnBtn   .setBounds (buttons.removeFromLeft (60));  buttons.removeFromLeft (4);
            clearAllBtn.setBounds (buttons.removeFromLeft (80));
            closeBtn   .setBounds (buttons.removeFromRight (80));
            r.removeFromBottom (6);
            lastMidiLabel.setBounds (r.removeFromBottom (20));
            r.removeFromBottom (4);
            table.setBounds (r);
        }

        int getNumRows() override { return mappings.size(); }

        void paintRowBackground (juce::Graphics& g, int, int, int, bool selected) override
        {
            g.fillAll (selected ? ns::Colours::tealAccent.withAlpha (0.45f)
                                : juce::Colours::transparentBlack);
        }

        void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
        {
            if (! juce::isPositiveAndBelow (row, mappings.size())) return;
            const auto& m = mappings.getReference (row);
            juce::String txt;
            switch (col)
            {
                case 1: txt = m.displayName.isNotEmpty() ? m.displayName : m.paramId; break;
                case 2: txt = (m.channel == 0) ? juce::String ("Omni") : juce::String (m.channel); break;
                case 3: txt = (m.type == MidiMsgType::CC) ? "CC"
                            : (m.type == MidiMsgType::Note) ? "Note" : "PC"; break;
                case 4: txt = juce::String (m.ccOrNote); break;
            }
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (txt, 6, 0, w - 12, h, juce::Justification::centredLeft);
        }

        void cellClicked (int row, int /*col*/, const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu()) return;
            if (! juce::isPositiveAndBelow (row, mappings.size())) return;
            const auto id = mappings.getReference (row).paramId;
            juce::PopupMenu m;
            m.addItem ("Remove mapping", [id] { App::get().getMIDILearn().clearMapping (id); });
            m.showMenuAsync ({});
        }

    private:
        void refresh()
        {
            mappings = App::get().getMIDILearn().getMappings();
            table.updateContent();
            table.repaint();
        }

        juce::TableListBox            table;
        juce::Array<MIDILearnMapping> mappings;
        juce::TextButton              addBtn, editBtn, learnBtn, clearAllBtn, closeBtn;
        juce::Label                   lastMidiLabel;

        void showAddEditDialog (juce::String existingParamId);
    };

    //==========================================================================
    // Sub-dialog: pick a parameter, channel, type and CC/Note number.
    //==========================================================================
    class MidiAddEdit : public juce::Component
    {
    public:
        MidiAddEdit (juce::String preselect)
        {
            setSize (380, 220);

            auto& reg = App::get().getMIDILearn();
            ids = reg.getRegisteredParamIds();

            paramLabel.setText  ("Parameter", juce::dontSendNotification);
            chanLabel .setText  ("Channel (0 = Omni)", juce::dontSendNotification);
            typeLabel .setText  ("Type", juce::dontSendNotification);
            numLabel  .setText  ("CC / Note (0-127)", juce::dontSendNotification);
            for (auto* l : { &paramLabel, &chanLabel, &typeLabel, &numLabel })
            {
                l->setColour (juce::Label::textColourId, juce::Colours::white);
                l->setFont (juce::Font (juce::FontOptions (12.0f)));
                addAndMakeVisible (*l);
            }

            int sel = -1;
            for (int i = 0; i < ids.size(); ++i)
            {
                paramBox.addItem (reg.getDisplayName (ids[i]) + "   [" + ids[i] + "]", i + 1);
                if (ids[i] == preselect) sel = i + 1;
            }
            if (sel > 0) paramBox.setSelectedId (sel, juce::dontSendNotification);
            else if (paramBox.getNumItems() > 0) paramBox.setSelectedItemIndex (0, juce::dontSendNotification);
            addAndMakeVisible (paramBox);

            typeBox.addItem ("CC",   1);
            typeBox.addItem ("Note", 2);
            typeBox.addItem ("PC",   3);
            typeBox.setSelectedId (1, juce::dontSendNotification);
            addAndMakeVisible (typeBox);

            chanEdit.setInputRestrictions (3, "0123456789");
            chanEdit.setText ("0", juce::dontSendNotification);
            numEdit .setInputRestrictions (3, "0123456789");
            numEdit .setText ("0", juce::dontSendNotification);
            addAndMakeVisible (chanEdit);
            addAndMakeVisible (numEdit);

            // If editing, pre-fill from existing mapping.
            if (preselect.isNotEmpty())
            {
                for (auto& m : reg.getMappings())
                    if (m.paramId == preselect)
                    {
                        const int tid = (m.type == MidiMsgType::CC)   ? 1
                                       : (m.type == MidiMsgType::Note) ? 2 : 3;
                        typeBox.setSelectedId (tid, juce::dontSendNotification);
                        chanEdit.setText (juce::String (m.channel),  juce::dontSendNotification);
                        numEdit .setText (juce::String (m.ccOrNote), juce::dontSendNotification);
                        break;
                    }
            }

            okBtn.setButtonText ("Save");
            okBtn.onClick = [this]
            {
                const int idx = paramBox.getSelectedItemIndex();
                if (! juce::isPositiveAndBelow (idx, ids.size())) return;
                const int tid = typeBox.getSelectedId();
                const auto ty = (tid == 1) ? MidiMsgType::CC
                              : (tid == 2) ? MidiMsgType::Note
                                           : MidiMsgType::PC;
                App::get().getMIDILearn().setMapping (
                    ids[idx],
                    chanEdit.getText().getIntValue(),
                    numEdit .getText().getIntValue(),
                    ty);
                close();
            };
            cancelBtn.setButtonText ("Cancel");
            cancelBtn.onClick = [this] { close(); };
            for (auto* b : { &okBtn, &cancelBtn })
            {
                b->setColour (juce::TextButton::buttonColourId,  ns::Colours::chipUnsel);
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                addAndMakeVisible (*b);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            const int rowH = 18, edH = 24, gap = 4;

            paramLabel.setBounds (r.removeFromTop (rowH));
            r.removeFromTop (gap);
            paramBox  .setBounds (r.removeFromTop (edH));
            r.removeFromTop (10);

            // Row: type + channel
            {
                auto labels = r.removeFromTop (rowH);
                const int half = labels.getWidth() / 2 - 4;
                typeLabel.setBounds (labels.removeFromLeft (half));
                labels.removeFromLeft (8);
                chanLabel.setBounds (labels);
                r.removeFromTop (gap);
                auto edits = r.removeFromTop (edH);
                typeBox .setBounds (edits.removeFromLeft (half));
                edits.removeFromLeft (8);
                chanEdit.setBounds (edits);
                r.removeFromTop (10);
            }

            // Row: cc/note
            numLabel.setBounds (r.removeFromTop (rowH));
            r.removeFromTop (gap);
            numEdit .setBounds (r.removeFromTop (edH).removeFromLeft (120));

            auto bottom = getLocalBounds().reduced (12).removeFromBottom (28);
            cancelBtn.setBounds (bottom.removeFromRight (80));
            bottom.removeFromRight (6);
            okBtn    .setBounds (bottom.removeFromRight (80));
        }

    private:
        void close()
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        }

        juce::StringArray ids;
        juce::Label       paramLabel, chanLabel, typeLabel, numLabel;
        juce::ComboBox    paramBox, typeBox;
        juce::TextEditor  chanEdit, numEdit;
        juce::TextButton  okBtn, cancelBtn;
    };

    void MidiAssignTable::showAddEditDialog (juce::String existingParamId)
    {
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle                  = existingParamId.isEmpty() ? "Add MIDI Assignment"
                                                                   : "Edit MIDI Assignment";
        o.dialogBackgroundColour       = ns::Colours::background;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar            = false;
        o.resizable                    = false;
        o.componentToCentreAround      = getAppMainWindow();
        o.content.setOwned (new MidiAddEdit (existingParamId));
        if (auto* dw = o.launchAsync())
            dw->setLookAndFeel (&ns::appWindowLNF());
    }
}

void showMidiAssignments()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle                  = "MIDI Assignments";
    o.dialogBackgroundColour       = ns::Colours::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar            = false;
    o.resizable                    = false;
    o.componentToCentreAround      = getAppMainWindow();
    o.content.setOwned (new MidiAssignTable());
    if (auto* dw = o.launchAsync())
        dw->setLookAndFeel (&ns::appWindowLNF());
}

//==============================================================================
// Footswitch wizard
//==============================================================================
namespace
{
    class FootswitchWizard : public juce::Component,
                             private juce::Timer
    {
    public:
        FootswitchWizard()
        {
            rowList.setOpaque (true);
            viewport.setViewedComponent (&rowList, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);

            header.setText ("FOOTSWITCH WIZARD  --  one-click bindings for live use",
                            juce::dontSendNotification);
            header.setJustificationType (juce::Justification::centred);
            header.setColour (juce::Label::textColourId, ns::Colours::accentGlow);
            header.setFont (juce::Font (juce::FontOptions (13.5f).withStyle ("Bold")));
            addAndMakeVisible (header);

            hint.setText ("Every learnable target in the app is listed below. "
                          "Press LEARN, then move/press the control on your MIDI footswitch "
                          "(CC, Note, or PC). It will be bound instantly. Press LEARN again "
                          "to cancel. X clears the binding. Filter narrows the list.",
                          juce::dontSendNotification);
            hint.setJustificationType (juce::Justification::centredLeft);
            hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
            hint.setFont (juce::Font (juce::FontOptions (11.0f)));
            hint.setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (hint);

            filterBox.setTextToShowWhenEmpty ("Filter (e.g. scene, bypass, mute)...",
                                              juce::Colours::white.withAlpha (0.35f));
            filterBox.onTextChange = [this] { rebuildRows(); };
            addAndMakeVisible (filterBox);

            openTableBtn.setButtonText ("Open MIDI Assignments...");
            openTableBtn.onClick = [] { ns::Dialogs::showMidiAssignments(); };
            clearAllBtn.setButtonText ("Clear All");
            clearAllBtn.onClick = []
            {
                for (auto& m : App::get().getMIDILearn().getMappings())
                    App::get().getMIDILearn().clearMapping (m.paramId);
            };
            closeBtn.setButtonText ("Close");
            closeBtn.onClick = [this]
            {
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (0);
            };
            addAndMakeVisible (openTableBtn);
            addAndMakeVisible (clearAllBtn);
            addAndMakeVisible (closeBtn);

            App::get().getMIDILearn().onChanged = [this]
            {
                // A new param might have been registered (rare) -- rebuild.
                rebuildRows();
                refresh();
            };

            rebuildRows();
            setSize (640, 560);
            startTimerHz (10); // refresh "LISTENING..." text live
        }

        ~FootswitchWizard() override
        {
            App::get().getMIDILearn().onChanged = nullptr;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (ns::Colours::background);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            header.setBounds (r.removeFromTop (26));
            r.removeFromTop (4);
            hint  .setBounds (r.removeFromTop (44));
            r.removeFromTop (6);

            auto buttons = r.removeFromBottom (30);
            openTableBtn.setBounds (buttons.removeFromLeft (180));
            buttons.removeFromLeft (6);
            clearAllBtn .setBounds (buttons.removeFromLeft (90));
            closeBtn    .setBounds (buttons.removeFromRight (90));
            r.removeFromBottom (8);

            filterBox.setBounds (r.removeFromTop (24));
            r.removeFromTop (6);

            viewport.setBounds (r);
            layoutRowList();
        }

    private:
        struct Entry  { juce::String paramId, title; };
        struct Row
        {
            juce::Label      title, binding;
            juce::TextButton learn, clear;
            juce::String     paramId;
        };

        // Inner component that hosts the row widgets so the viewport can
        // scroll them as a unit.
        struct RowList : juce::Component
        {
            void paint (juce::Graphics& g) override { g.fillAll (ns::Colours::background); }
        } rowList;

        void layoutRowList()
        {
            const int rowH = 30;
            const int totalH = (int) rows.size() * rowH + 4;
            const int w = juce::jmax (viewport.getWidth() - 16, 200);
            rowList.setSize (w, juce::jmax (totalH, viewport.getHeight()));

            int y = 2;
            for (auto& row : rows)
            {
                auto rr = juce::Rectangle<int> (0, y, w, rowH).reduced (4, 2);
                row->clear  .setBounds (rr.removeFromRight (28));
                rr.removeFromRight (4);
                row->learn  .setBounds (rr.removeFromRight (74));
                rr.removeFromRight (6);
                row->binding.setBounds (rr.removeFromRight (180));
                row->title  .setBounds (rr);
                y += rowH;
            }
        }

        void rebuildRows()
        {
            // Pull every learnable target the app has registered. This is
            // authoritative: scene recalls, signal-chain bypasses, mutes,
            // tap tempo, plus anything the per-knob registrations added.
            auto& reg = App::get().getMIDILearn();
            const auto allIds = reg.getRegisteredParamIds();

            // Preferred ordering: well-known "footswitch first" entries on
            // top in a curated order, everything else after, alphabetised.
            const juce::StringArray top {
                "tempo.tap",
                "ab.toggle",
                "output.mute",
                "input.mute",
                "nam.bypass",
                "nam.normalize",
                "preFx.bypass",
                "postFx.bypass",
                "scene.recall.0",
                "scene.recall.1",
                "scene.recall.2",
                "scene.recall.3"
            };

            juce::StringArray ordered;
            for (auto& t : top)
                if (allIds.contains (t)) ordered.add (t);
            juce::StringArray rest;
            for (auto& id : allIds)
                if (! top.contains (id)) rest.add (id);
            rest.sort (false);
            ordered.addArray (rest);

            // Filter.
            const auto needle = filterBox.getText().trim().toLowerCase();
            entries.clear();
            for (auto& id : ordered)
            {
                const auto dn = reg.getDisplayName (id);
                if (needle.isNotEmpty()
                    && ! id.containsIgnoreCase (needle)
                    && ! dn.containsIgnoreCase (needle))
                    continue;
                entries.push_back ({ id, dn });
            }

            // Re-allocate row widgets. Cheap -- under 100 rows in any
            // realistic build.
            for (auto& r : rows)
            {
                removeChildComponent (&r->title);
                removeChildComponent (&r->binding);
                removeChildComponent (&r->learn);
                removeChildComponent (&r->clear);
            }
            rows.clear();
            rows.reserve (entries.size());
            for (const auto& e : entries)
            {
                auto row = std::make_unique<Row>();
                row->paramId = e.paramId;
                row->title.setText (e.title, juce::dontSendNotification);
                row->title.setColour (juce::Label::textColourId, juce::Colours::white);
                row->title.setFont (juce::Font (juce::FontOptions (13.5f).withStyle ("Bold")));
                row->binding.setColour (juce::Label::textColourId,
                                        juce::Colours::white.withAlpha (0.65f));
                row->binding.setFont (juce::Font (juce::FontOptions (12.5f)));
                row->learn.setButtonText ("LEARN");
                row->learn.setTooltip ("Press LEARN, then tap your footswitch / MIDI controller (CC, Note or PC).");
                const auto pid = e.paramId;
                row->learn.onClick = [pid]
                {
                    auto& r = App::get().getMIDILearn();
                    if (r.isLearning() && r.currentLearnTarget() == pid)
                        r.cancelLearn();
                    else
                        r.beginLearn (pid);
                };
                row->clear.setButtonText ("X");
                row->clear.setTooltip ("Remove this binding.");
                row->clear.onClick = [pid] { App::get().getMIDILearn().clearMapping (pid); };

                rowList.addAndMakeVisible (row->title);
                rowList.addAndMakeVisible (row->binding);
                rowList.addAndMakeVisible (row->learn);
                rowList.addAndMakeVisible (row->clear);
                rows.push_back (std::move (row));
            }

            layoutRowList();
            refresh();
        }

        void refresh()
        {
            auto& reg = App::get().getMIDILearn();
            const auto mappings = reg.getMappings();
            const auto learning = reg.isLearning();
            const auto learnTgt = reg.currentLearnTarget();

            for (auto& rowPtr : rows)
            {
                auto& row = *rowPtr;

                const MIDILearnMapping* mp = nullptr;
                for (auto& m : mappings)
                    if (m.paramId == row.paramId) { mp = &m; break; }

                if (learning && learnTgt == row.paramId)
                {
                    row.binding.setText ("LISTENING...", juce::dontSendNotification);
                    row.binding.setColour (juce::Label::textColourId,
                                           ns::Colours::accentGlow);
                    row.learn.setButtonText ("CANCEL");
                }
                else if (mp != nullptr)
                {
                    const auto ch = (mp->channel == 0) ? juce::String ("omni")
                                                       : juce::String ("ch ") + juce::String (mp->channel);
                    const auto kind = (mp->type == MidiMsgType::CC)   ? juce::String ("CC ")
                                    : (mp->type == MidiMsgType::Note) ? juce::String ("Note ")
                                                                       : juce::String ("PC ");
                    row.binding.setText (kind + juce::String (mp->ccOrNote)
                                          + "  (" + ch + ")",
                                         juce::dontSendNotification);
                    row.binding.setColour (juce::Label::textColourId,
                                           juce::Colours::white.withAlpha (0.85f));
                    row.learn.setButtonText ("LEARN");
                }
                else
                {
                    row.binding.setText ("(unbound)", juce::dontSendNotification);
                    row.binding.setColour (juce::Label::textColourId,
                                           juce::Colours::white.withAlpha (0.45f));
                    row.learn.setButtonText ("LEARN");
                }
            }
        }

        void timerCallback() override { refresh(); }

        juce::Label header, hint;
        juce::TextEditor filterBox;
        juce::Viewport viewport;
        juce::TextButton openTableBtn, clearAllBtn, closeBtn;
        std::vector<Entry> entries;
        std::vector<std::unique_ptr<Row>> rows;
    };
} // anonymous namespace

void showFootswitchWizard()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle                  = "Footswitch Wizard";
    o.dialogBackgroundColour       = ns::Colours::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar            = false;
    o.resizable                    = false;
    o.componentToCentreAround      = getAppMainWindow();
    o.content.setOwned (new FootswitchWizard());
    if (auto* dw = o.launchAsync())
        dw->setLookAndFeel (&ns::appWindowLNF());
}

//==============================================================================
// About / App Info
//==============================================================================
namespace
{
    class AboutPanel : public juce::Component
    {
    public:
        AboutPanel()
        {
            bg = juce::ImageCache::getFromMemory (BinaryData::ls_png,
                                                  BinaryData::ls_pngSize);

            // Clickable Tone3000 link in the ACKNOWLEDGEMENTS body.
            // Sits as an overlay so the user can click straight from the
            // About panel into their capture-sharing community.
            tone3000Link.setButtonText ("Tone3000");
            tone3000Link.setURL (juce::URL ("https://www.tone3000.com/search"));
            tone3000Link.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")),
                                   false, juce::Justification::centred);
            tone3000Link.setColour (juce::HyperlinkButton::textColourId,
                                    ns::Colours::accentGlow);
            tone3000Link.setTooltip ("Open tone3000.com in your browser");
            addAndMakeVisible (tone3000Link);

            setSize (560, 640);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xFF050507));

            auto r = getLocalBounds().toFloat();

            // Top 52%: artwork. Letterboxed so the logo never crops.
            const float artH = std::floor (r.getHeight() * 0.52f);
            auto artArea = r.removeFromTop (artH);
            if (bg.isValid())
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (bg, artArea,
                             juce::RectanglePlacement::centred
                             | juce::RectanglePlacement::onlyReduceInSize, false);
            }

            // Hairline divider.
            g.setColour (juce::Colour (0x33FFFFFF));
            g.fillRect (juce::Rectangle<float> (r.getX() + 32.0f, r.getY(),
                                                r.getWidth() - 64.0f, 1.0f));

            // ---- Info zone ----
            auto info = r.reduced (28.0f, 14.0f);

            // Version (no JUCE version per user request).
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
            g.drawFittedText (juce::String ("NeuralStage  v") + JUCE_APPLICATION_VERSION_STRING,
                              info.removeFromTop (18.0f).toNearestInt(),
                              juce::Justification::centred, 1);

            // Tagline.
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Italic")));
            g.drawFittedText ("Live guitar rig for plugin players",
                              info.removeFromTop (14.0f).toNearestInt(),
                              juce::Justification::centred, 1);

            info.removeFromTop (10.0f);

            // ---- License heading + body ----
            g.setColour (ns::Colours::accentGlow);
            g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
            g.drawFittedText ("LICENSE",
                              info.removeFromTop (13.0f).toNearestInt(),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            auto licBody = info.removeFromTop (42.0f);
            g.drawFittedText (
                "This software is provided free of charge, AS IS, without warranty "
                "of any kind, express or implied. Use of this software is at your own risk. "
                "The author shall not be liable for any damages arising from its use.",
                licBody.toNearestInt(),
                juce::Justification::centredTop, 3);

            info.removeFromTop (8.0f);

            // ---- Terms of Use heading + body ----
            g.setColour (ns::Colours::accentGlow);
            g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
            g.drawFittedText ("TERMS OF USE",
                              info.removeFromTop (13.0f).toNearestInt(),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            auto termsBody = info.removeFromTop (42.0f);
            g.drawFittedText (
                "Personal and commercial use permitted. Redistribution, resale or "
                "rebranding of the software or its components is not permitted "
                "without prior written consent from the author.",
                termsBody.toNearestInt(),
                juce::Justification::centredTop, 3);

            info.removeFromTop (8.0f);

            // ---- Acknowledgements heading + body ----
            g.setColour (ns::Colours::accentGlow);
            g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
            g.drawFittedText ("ACKNOWLEDGEMENTS",
                              info.removeFromTop (13.0f).toNearestInt(),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            auto ackBody = info.removeFromTop (30.0f);
            g.drawFittedText (
                "Neural amp modelling by Steven Atkinson (Neural Amp Modeler), "
                "and the wider open-source audio community -- thank you.",
                ackBody.toNearestInt(),
                juce::Justification::centredTop, 2);

            // Clickable Tone3000 link sits as its own centred line just
            // below the ack body. Painted preamble + the HyperlinkButton
            // share two stacked rows so the link is unambiguous.
            auto preRow  = info.removeFromTop (14.0f);
            auto linkRow = info.removeFromTop (16.0f);
            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.drawFittedText ("Capture-sharing community via",
                              preRow.toNearestInt(),
                              juce::Justification::centred, 1);
            lastLinkRow = linkRow.toNearestInt();
            // Reposition the link directly here so it lines up with whatever
            // flow geometry paint() ended up with -- avoids resized() being
            // out of sync with paint() on first show.
            {
                const int w = 90;
                tone3000Link.setBounds (lastLinkRow.getCentreX() - w / 2,
                                         lastLinkRow.getY(),
                                         w, lastLinkRow.getHeight());
            }

            info.removeFromTop (10.0f);

            // ---- Copyright footer ----
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawFittedText ("(c) 2026 Atij 666 Studio  -  Created by Atij  -  All rights reserved",
                              info.removeFromTop (13.0f).toNearestInt(),
                              juce::Justification::centred, 1);

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Italic")));
            g.drawFittedText ("Click anywhere or press Esc to close",
                              info.removeFromTop (12.0f).toNearestInt(),
                              juce::Justification::centred, 1);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // Don't dismiss the dialog if the user clicked on the Tone3000
            // link -- they want to follow the link, not close the panel.
            if (tone3000Link.getBounds().contains (e.getPosition()))
                return;
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        }

        void resized() override
        {
            // Position the Tone3000 link on whatever row paint() reserved
            // for it. paint() runs first on initial show, so by the time
            // the user can interact the rect is populated. The fallback
            // bounds keep the button hittable even before the first paint.
            auto r = lastLinkRow.isEmpty()
                       ? juce::Rectangle<int> (getWidth() / 2 - 60,
                                                getHeight() - 130, 120, 16)
                       : lastLinkRow;
            const int w = 90;
            tone3000Link.setBounds (r.getCentreX() - w / 2, r.getY(), w, r.getHeight());
        }

    private:
        juce::Image           bg;
        juce::HyperlinkButton tone3000Link;
        juce::Rectangle<int>  lastLinkRow;
    };
}

void showAboutDialog()
{
    // Custom DialogWindow so a click anywhere OUTSIDE the dialog (which is a
    // modal-level "input attempt") dismisses it -- splash convention.
    // Esc, the title-bar X, and clicks INSIDE the panel also close it.
    struct AboutDialog : juce::DialogWindow
    {
        AboutDialog()
            : juce::DialogWindow ("About NeuralStage",
                                  juce::Colour (0xFF050507),
                                  /*escapeKeyTriggersCloseButton = */ true,
                                  /*addToDesktop = */ true)
        {
            setUsingNativeTitleBar (false);
            setLookAndFeel (&ns::appWindowLNF());
            setResizable (false, false);
            setContentOwned (new AboutPanel(), true);
            if (auto* app = getAppMainWindow())
                centreAroundComponent (app, getWidth(), getHeight());
            else
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
            enterModalState (true,
                             juce::ModalCallbackFunction::create ([this] (int) { delete this; }),
                             false);
        }

        ~AboutDialog() override { setLookAndFeel (nullptr); }

        void closeButtonPressed() override   { exitModalState (0); }
        void inputAttemptWhenModal() override { exitModalState (0); }
    };

    new AboutDialog(); // self-deletes via the ModalCallback above
}

//==============================================================================
// openManual
//
// Resolves the user manual relative to the installed executable and hands it
// to the OS default viewer (Edge/Acrobat for PDF, Notepad/VS Code for MD).
// Looks in <app>\Docs\NeuralStage-Manual.pdf first, then falls back to .md.
//==============================================================================
void openManual()
{
    auto appDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                       .getParentDirectory();

    juce::File pdf = appDir.getChildFile ("Docs").getChildFile ("NeuralStage-Manual.pdf");
    juce::File md  = appDir.getChildFile ("Docs").getChildFile ("NeuralStage-Manual.md");

    // Dev-tree fallback: when running from Builds\..\Release\NeuralStage.exe
    // the Docs/ folder lives a few directories up. Walk up looking for it.
    auto findInTree = [] (juce::File start, const juce::String& leaf) -> juce::File
    {
        for (int i = 0; i < 6; ++i)
        {
            auto candidate = start.getChildFile ("Docs").getChildFile (leaf);
            if (candidate.existsAsFile()) return candidate;
            auto parent = start.getParentDirectory();
            if (parent == start) break;
            start = parent;
        }
        return {};
    };

    if (! pdf.existsAsFile())
    {
        auto found = findInTree (appDir, "NeuralStage-Manual.pdf");
        if (found.existsAsFile()) pdf = found;
    }
    if (! md.existsAsFile())
    {
        auto found = findInTree (appDir, "NeuralStage-Manual.md");
        if (found.existsAsFile()) md = found;
    }

    juce::File target = pdf.existsAsFile() ? pdf
                       : (md.existsAsFile() ? md : juce::File{});

    if (! target.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Manual not found",
            "Could not locate NeuralStage-Manual.pdf or .md next to the application.\n\n"
            "Expected: " + appDir.getChildFile ("Docs").getFullPathName());
        return;
    }

    target.startAsProcess();
}

//==============================================================================
// Non-modal floating tool window helper.
//
// DialogWindow::launchAsync enters a modal-component loop that blocks input
// to the rest of the editor, which prevents scene/preset switching while a
// tool window is open. For tools like Looper and Backing Track the user
// needs to keep playing and switching scenes while the panel stays visible,
// so we host the content in a free DocumentWindow instead. A static map of
// "kind" -> window ensures clicking the menu item a second time brings the
// existing window to the front rather than spawning a duplicate.
//==============================================================================
namespace
{
    class ToolWindow : public juce::DocumentWindow
    {
    public:
        ToolWindow (const juce::String& title, juce::Component* content)
            : juce::DocumentWindow (title, ns::Colours::background,
                                    juce::DocumentWindow::closeButton, true)
        {
            setUsingNativeTitleBar (false);
            setLookAndFeel (&ns::appWindowLNF());
            setResizable (false, false);
            setContentOwned (content, true);
            // Keep tool windows above the main app so scene switches and other
            // clicks on the main window don't bury them.
            setAlwaysOnTop (true);
            centreAroundComponent (getAppMainWindow(),
                                   content->getWidth(), content->getHeight());
            setVisible (true);
        }
        ~ToolWindow() override { setLookAndFeel (nullptr); }
        void closeButtonPressed() override { delete this; } // self-delete via owner reset
    };

    struct ToolWindowRegistry
    {
        std::map<juce::String, juce::Component::SafePointer<ToolWindow>> windows;

        void showOrFocus (const juce::String& key, const juce::String& title,
                          std::function<juce::Component*()> contentFactory)
        {
            auto it = windows.find (key);
            if (it != windows.end() && it->second != nullptr)
            {
                it->second->toFront (true);
                return;
            }
            auto* w = new ToolWindow (title, contentFactory());
            windows[key] = w;
        }
    };

    ToolWindowRegistry& toolRegistry()
    {
        static ToolWindowRegistry r;
        return r;
    }
}

//==============================================================================
// Looper transport
//==============================================================================
namespace
{
    class LooperPanel : public juce::Component, private juce::Timer
    {
    public:
        LooperPanel()
        {
            setSize (480, 320);

            auto styleBtn = [] (juce::TextButton& b, juce::Colour bg)
            {
                b.setColour (juce::TextButton::buttonColourId,  bg);
                b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
            };

            styleBtn (recBtn,   juce::Colour (0xffaa2a2a));
            styleBtn (playBtn,  juce::Colour (0xff2a7a3a));
            styleBtn (stopBtn,  ns::Colours::chipUnsel);
            styleBtn (clearBtn, ns::Colours::chipUnsel);

            recBtn  .setButtonText ("REC / OVERDUB");
            playBtn .setButtonText ("PLAY");
            stopBtn .setButtonText ("STOP");
            clearBtn.setButtonText ("CLEAR");

            recBtn  .onClick = []{ App::get().getAudioEngine().getLooper().tapRecord(); };
            playBtn .onClick = []{ App::get().getAudioEngine().getLooper().tapPlay();   };
            stopBtn .onClick = []{ App::get().getAudioEngine().getLooper().tapStop();   };
            clearBtn.onClick = [this]
            {
                App::get().getAudioEngine().getLooper().tapClear();
                repaint();
            };

            for (auto* b : { &recBtn, &playBtn, &stopBtn, &clearBtn })
                addAndMakeVisible (*b);

            // ----- Count-in + metronome toggles -----
            auto styleToggle = [] (juce::TextButton& b)
            {
                b.setClickingTogglesState (true);
                b.setColour (juce::TextButton::buttonColourId,   ns::Colours::chipUnsel);
                b.setColour (juce::TextButton::buttonOnColourId, ns::Colours::tealAccent);
                b.setColour (juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha (0.7f));
                b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            };
            styleToggle (countInBtn);
            styleToggle (metBtn);

            auto& lp = App::get().getAudioEngine().getLooper();
            countInBtn.setButtonText (lp.isCountInEnabled() ? "COUNT-IN: ON" : "COUNT-IN: OFF");
            countInBtn.setToggleState (lp.isCountInEnabled(), juce::dontSendNotification);
            countInBtn.onClick = [this]
            {
                const bool on = countInBtn.getToggleState();
                App::get().getAudioEngine().getLooper().setCountInEnabled (on);
                countInBtn.setButtonText (on ? "COUNT-IN: ON" : "COUNT-IN: OFF");
            };

            metBtn.setButtonText (lp.isMetronomeOn() ? "METRONOME: ON" : "METRONOME: OFF");
            metBtn.setToggleState (lp.isMetronomeOn(), juce::dontSendNotification);
            metBtn.onClick = [this]
            {
                const bool on = metBtn.getToggleState();
                App::get().getAudioEngine().getLooper().setMetronomeOn (on);
                metBtn.setButtonText (on ? "METRONOME: ON" : "METRONOME: OFF");
            };

            addAndMakeVisible (countInBtn);
            addAndMakeVisible (metBtn);

            mixSlider.setRange (0.0, 1.5, 0.001);
            mixSlider.setValue (lp.getMix(), juce::dontSendNotification);
            mixSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            mixSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
            mixSlider.setColour (juce::Slider::backgroundColourId, ns::Colours::chipUnsel);
            mixSlider.setColour (juce::Slider::trackColourId,      ns::Colours::accent);
            mixSlider.onValueChange = [this]
            {
                App::get().getAudioEngine().getLooper().setMix ((float) mixSlider.getValue());
            };
            addAndMakeVisible (mixSlider);

            mixLabel.setText ("Loop Level", juce::dontSendNotification);
            mixLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
            addAndMakeVisible (mixLabel);

            metLevelSlider.setRange (0.0, 1.0, 0.001);
            metLevelSlider.setValue (lp.getMetronomeLevel(), juce::dontSendNotification);
            metLevelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            metLevelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
            metLevelSlider.setColour (juce::Slider::backgroundColourId, ns::Colours::chipUnsel);
            metLevelSlider.setColour (juce::Slider::trackColourId,      ns::Colours::accent);
            metLevelSlider.onValueChange = [this]
            {
                App::get().getAudioEngine().getLooper().setMetronomeLevel ((float) metLevelSlider.getValue());
            };
            addAndMakeVisible (metLevelSlider);

            metLevelLabel.setText ("Click Level", juce::dontSendNotification);
            metLevelLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
            addAndMakeVisible (metLevelLabel);

            startTimerHz (30);
        }

        ~LooperPanel() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (ns::Colours::background);

            auto& lp = App::get().getAudioEngine().getLooper();
            const auto s = lp.getState();

            // Status banner.
            auto top = getLocalBounds().removeFromTop (40).reduced (12, 6);
            juce::String label;
            juce::Colour col;
            switch (s)
            {
                case nl::Looper::State::Idle:      label = "IDLE -- tap REC to start";    col = juce::Colours::white.withAlpha (0.55f); break;
                case nl::Looper::State::CountIn:   label = "COUNT-IN: " + juce::String (lp.getCountInBeatsRemaining());
                                                                                          col = juce::Colour (0xffe0a040); break;
                case nl::Looper::State::Recording: label = "RECORDING";                   col = juce::Colour (0xffe04040); break;
                case nl::Looper::State::Playing:   label = "PLAYING";                     col = juce::Colour (0xff40c060); break;
                case nl::Looper::State::Overdub:   label = "OVERDUB";                     col = juce::Colour (0xffe0a040); break;
                case nl::Looper::State::Stopped:   label = "STOPPED";                     col = juce::Colours::white.withAlpha (0.55f); break;
            }
            g.setColour (col);
            g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold")));
            g.drawFittedText (label, top, juce::Justification::centredLeft, 1);

            // Right side: loop length + tempo.
            const auto secs = lp.getLengthSeconds();
            const double bpm = lp.getBpm();
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawFittedText (juce::String (secs, 2) + " s  |  " + juce::String (bpm, 1) + " BPM",
                              top, juce::Justification::centredRight, 1);

            // Progress bar -- during CountIn shows count-in progress, during
            // Recording/Playing/Overdub shows loop position.
            auto prog = getLocalBounds().removeFromTop (52).removeFromBottom (10).reduced (12, 0);
            g.setColour (ns::Colours::chipUnsel);
            g.fillRoundedRectangle (prog.toFloat(), 3.0f);

            const float progFrac = lp.getProgress();
            const bool  showProg = (s == nl::Looper::State::CountIn)
                                || (s == nl::Looper::State::Recording)
                                || (s == nl::Looper::State::Playing)
                                || (s == nl::Looper::State::Overdub);
            if (showProg && progFrac > 0.0f)
            {
                auto fill = prog.toFloat();
                fill.setWidth (fill.getWidth() * progFrac);
                g.setColour (col);
                g.fillRoundedRectangle (fill, 3.0f);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromTop (60); // banner + progress

            // Row 1: REC | PLAY | STOP | CLEAR
            auto row = r.removeFromTop (44).reduced (12, 4);
            const int gap = 6;
            const int w = (row.getWidth() - gap * 3) / 4;
            recBtn  .setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap);
            playBtn .setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap);
            stopBtn .setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap);
            clearBtn.setBounds (row);

            r.removeFromTop (10);

            // Row 2: COUNT-IN | METRONOME toggles
            auto trow = r.removeFromTop (36).reduced (12, 0);
            const int tgap = 8;
            const int tw = (trow.getWidth() - tgap) / 2;
            countInBtn.setBounds (trow.removeFromLeft (tw));
            trow.removeFromLeft (tgap);
            metBtn    .setBounds (trow);

            r.removeFromTop (10);

            // Row 3: Loop Level fader
            auto mixRow = r.removeFromTop (32).reduced (12, 0);
            mixLabel .setBounds (mixRow.removeFromLeft (90));
            mixSlider.setBounds (mixRow);

            r.removeFromTop (6);

            // Row 4: Click Level fader
            auto metRow = r.removeFromTop (32).reduced (12, 0);
            metLevelLabel .setBounds (metRow.removeFromLeft (90));
            metLevelSlider.setBounds (metRow);
        }

        void timerCallback() override { repaint(); }

    private:
        juce::TextButton recBtn, playBtn, stopBtn, clearBtn;
        juce::TextButton countInBtn, metBtn;
        juce::Slider     mixSlider, metLevelSlider;
        juce::Label      mixLabel, metLevelLabel;
    };
}

void showLooperDialog()
{
    toolRegistry().showOrFocus ("looper", "Looper",
        [] () -> juce::Component* { return new LooperPanel(); });
}

// =========================================================================
// Backing Track
// =========================================================================

class BackingTrackPanel : public juce::Component, private juce::Timer
{
public:
    BackingTrackPanel()
    {
        setSize (480, 230);

        // ---- colour helpers ----
        auto styleBtn = [] (juce::TextButton& b, juce::Colour bg)
        {
            b.setColour (juce::TextButton::buttonColourId,  bg);
            b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
        };

        styleBtn (loadBtn,  ns::Colours::accent);
        styleBtn (playBtn,  juce::Colour (0xff2a7a3a));
        styleBtn (stopBtn,  ns::Colours::chipUnsel);
        styleBtn (loopBtn,  ns::Colours::chipUnsel);

        loadBtn.setButtonText ("LOAD FILE");
        playBtn.setButtonText ("PLAY / PAUSE");
        stopBtn.setButtonText ("STOP");
        loopBtn.setButtonText ("LOOP: OFF");
        loopBtn.setClickingTogglesState (true);

        loadBtn.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser> (
                "Load Backing Track",
                juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

            fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    const auto result = fc.getResult();
                    if (! result.existsAsFile()) return;
                    if (! App::get().getAudioEngine().getBackingTrack().load (result))
                    {
                        ns::ThemedAlerts::showWarning (
                            "Load failed",
                            "Could not read \"" + result.getFileName() + "\".\n"
                            "Supported formats: WAV, AIFF, FLAC, OGG, MP3.");
                    }
                    repaint();
                });
        };

        playBtn.onClick = []
        {
            auto& bt = App::get().getAudioEngine().getBackingTrack();
            if (bt.isPlaying()) bt.pause();
            else                bt.play();
        };

        stopBtn.onClick = [] { App::get().getAudioEngine().getBackingTrack().stop(); };

        loopBtn.onStateChange = [this]
        {
            const bool on = loopBtn.getToggleState();
            App::get().getAudioEngine().getBackingTrack().setLoop (on);
            loopBtn.setButtonText (on ? "LOOP: ON" : "LOOP: OFF");
            loopBtn.setColour (juce::TextButton::buttonColourId,
                               on ? ns::Colours::tealAccent : ns::Colours::chipUnsel);
        };

        for (auto* b : { &loadBtn, &playBtn, &stopBtn, &loopBtn })
            addAndMakeVisible (*b);

        // Level fader
        levelSlider.setRange (0.0, 2.0, 0.001);
        levelSlider.setValue (App::get().getAudioEngine().getBackingTrack().getLevel(),
                              juce::dontSendNotification);
        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        levelSlider.setColour (juce::Slider::backgroundColourId, ns::Colours::chipUnsel);
        levelSlider.setColour (juce::Slider::trackColourId,      ns::Colours::accent);
        levelSlider.onValueChange = []
        {
            App::get().getAudioEngine().getBackingTrack()
                .setLevel ((float) App::get().getAudioEngine().getBackingTrack().getLevel());
        };
        // Simpler direct binding:
        levelSlider.onValueChange = [this]
        {
            App::get().getAudioEngine().getBackingTrack()
                .setLevel ((float) levelSlider.getValue());
        };
        addAndMakeVisible (levelSlider);

        levelLabel.setText ("Track Level", juce::dontSendNotification);
        levelLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        addAndMakeVisible (levelLabel);

        startTimerHz (20);
    }

    ~BackingTrackPanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ns::Colours::background);

        auto& bt = App::get().getAudioEngine().getBackingTrack();

        // Status / filename banner
        auto top = getLocalBounds().removeFromTop (40).reduced (12, 6);

        juce::String statusText;
        juce::Colour statusCol;

        if (! bt.isLoaded())
        {
            statusText = "No file loaded -- click LOAD FILE";
            statusCol  = juce::Colours::white.withAlpha (0.45f);
        }
        else if (bt.isPlaying())
        {
            statusText = "PLAYING  |  " + bt.getFileName();
            statusCol  = juce::Colour (0xff40c060);
        }
        else
        {
            statusText = "PAUSED  |  " + bt.getFileName();
            statusCol  = juce::Colours::white.withAlpha (0.70f);
        }

        g.setColour (statusCol);
        g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
        g.drawFittedText (statusText, top, juce::Justification::centredLeft, 1);

        // Time readout
        if (bt.isLoaded())
        {
            const double pos = bt.getPositionSeconds();
            const double len = bt.getLengthSeconds();
            auto toMMSS = [] (double s) -> juce::String
            {
                const int m = (int) s / 60;
                const int sec = (int) s % 60;
                return juce::String (m) + ":" + juce::String (sec).paddedLeft ('0', 2);
            };
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawFittedText (toMMSS (pos) + " / " + toMMSS (len),
                              top, juce::Justification::centredRight, 1);

            // Progress bar
            auto prog = getLocalBounds().removeFromTop (52).removeFromBottom (10).reduced (12, 0);
            g.setColour (ns::Colours::chipUnsel);
            g.fillRoundedRectangle (prog.toFloat(), 3.0f);
            auto fill = prog.toFloat();
            fill.setWidth (fill.getWidth() * (float) bt.getProgress());
            g.setColour (bt.isPlaying() ? juce::Colour (0xff40c060)
                                        : juce::Colours::white.withAlpha (0.4f));
            g.fillRoundedRectangle (fill, 3.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (60); // banner + progress bar

        // Row 1: LOAD | PLAY/PAUSE | STOP | LOOP
        auto row1 = r.removeFromTop (44).reduced (12, 4);
        const int gap = 6;
        const int w1 = (row1.getWidth() - gap * 3) / 4;
        loadBtn.setBounds (row1.removeFromLeft (w1)); row1.removeFromLeft (gap);
        playBtn.setBounds (row1.removeFromLeft (w1)); row1.removeFromLeft (gap);
        stopBtn.setBounds (row1.removeFromLeft (w1)); row1.removeFromLeft (gap);
        loopBtn.setBounds (row1);

        r.removeFromTop (12);

        // Row 2: Level fader
        auto row2 = r.removeFromTop (32).reduced (12, 0);
        levelLabel .setBounds (row2.removeFromLeft (90));
        levelSlider.setBounds (row2);
    }

    void timerCallback() override { repaint(); }

private:
    juce::TextButton loadBtn, playBtn, stopBtn, loopBtn;
    juce::Slider     levelSlider;
    juce::Label      levelLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

void showBackingTrackDialog()
{
    toolRegistry().showOrFocus ("backingTrack", "Backing Track",
        [] () -> juce::Component* { return new BackingTrackPanel(); });
}

//==============================================================================
// Built-in noise gate settings
//
// The post-FX safety gate sits at the end of the chain and trims hum / noise
// floor between phrases. It is intentionally independent of any user-loaded
// gate plugin so a player can rely on a clean stop-of-signal even when no
// third-party gate is in the chain. Defaults: -60 dB threshold, 3 ms attack,
// 120 ms release, 20 ms hold, OFF.
//==============================================================================
namespace
{
    class NoiseGatePanel : public juce::Component, private juce::Timer
    {
    public:
        NoiseGatePanel()
        {
            setSize (440, 320);

            auto& g = App::get().getAudioEngine().getGate();

            enabledBtn.setButtonText (g.isEnabled() ? "GATE: ON" : "GATE: OFF");
            enabledBtn.setClickingTogglesState (true);
            enabledBtn.setToggleState (g.isEnabled(), juce::dontSendNotification);
            enabledBtn.setColour (juce::TextButton::buttonColourId,   ns::Colours::chipUnsel);
            enabledBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a7a3a));
            enabledBtn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha (0.7f));
            enabledBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            enabledBtn.onClick = [this]
            {
                const bool on = enabledBtn.getToggleState();
                App::get().getAudioEngine().getGate().setEnabled (on);
                enabledBtn.setButtonText (on ? "GATE: ON" : "GATE: OFF");
            };
            addAndMakeVisible (enabledBtn);

            auto styleSlider = [] (juce::Slider& s)
            {
                s.setSliderStyle (juce::Slider::LinearHorizontal);
                s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
                s.setColour (juce::Slider::backgroundColourId, ns::Colours::chipUnsel);
                s.setColour (juce::Slider::trackColourId,      ns::Colours::accent);
            };
            auto styleLabel = [] (juce::Label& l, const juce::String& t)
            {
                l.setText (t, juce::dontSendNotification);
                l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
            };

            // Threshold (-80..0 dB).
            threshSlider.setRange (-80.0, 0.0, 0.1);
            threshSlider.setValue (g.getThresholdDb(), juce::dontSendNotification);
            threshSlider.setTextValueSuffix (" dB");
            threshSlider.onValueChange = [this]
            {
                App::get().getAudioEngine().getGate().setThresholdDb ((float) threshSlider.getValue());
            };
            styleSlider (threshSlider);
            styleLabel  (threshLabel, "Threshold");
            addAndMakeVisible (threshSlider);
            addAndMakeVisible (threshLabel);

            // Attack (0.1..50 ms).
            attackSlider.setRange (0.1, 50.0, 0.1);
            attackSlider.setSkewFactorFromMidPoint (5.0);
            attackSlider.setValue (g.getAttackMs(), juce::dontSendNotification);
            attackSlider.setTextValueSuffix (" ms");
            attackSlider.onValueChange = [this]
            {
                App::get().getAudioEngine().getGate().setAttackMs ((float) attackSlider.getValue());
            };
            styleSlider (attackSlider);
            styleLabel  (attackLabel, "Attack");
            addAndMakeVisible (attackSlider);
            addAndMakeVisible (attackLabel);

            // Release (5..2000 ms).
            releaseSlider.setRange (5.0, 2000.0, 1.0);
            releaseSlider.setSkewFactorFromMidPoint (200.0);
            releaseSlider.setValue (g.getReleaseMs(), juce::dontSendNotification);
            releaseSlider.setTextValueSuffix (" ms");
            releaseSlider.onValueChange = [this]
            {
                App::get().getAudioEngine().getGate().setReleaseMs ((float) releaseSlider.getValue());
            };
            styleSlider (releaseSlider);
            styleLabel  (releaseLabel, "Release");
            addAndMakeVisible (releaseSlider);
            addAndMakeVisible (releaseLabel);

            // Hold (0..500 ms).
            holdSlider.setRange (0.0, 500.0, 1.0);
            holdSlider.setValue (g.getHoldMs(), juce::dontSendNotification);
            holdSlider.setTextValueSuffix (" ms");
            holdSlider.onValueChange = [this]
            {
                App::get().getAudioEngine().getGate().setHoldMs ((float) holdSlider.getValue());
            };
            styleSlider (holdSlider);
            styleLabel  (holdLabel, "Hold");
            addAndMakeVisible (holdSlider);
            addAndMakeVisible (holdLabel);

            // Help blurb.
            helpLabel.setText (
                "Built-in safety gate -- runs AFTER any plugin in the pre-FX or post-FX "
                "chain. Use to silence amp hiss and pickup hum between phrases.",
                juce::dontSendNotification);
            helpLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
            helpLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
            helpLabel.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (helpLabel);

            startTimerHz (10); // re-sync UI if state changes externally (preset / scene recall)
        }

        ~NoiseGatePanel() override { stopTimer(); }

        void paint (juce::Graphics& g) override { g.fillAll (ns::Colours::background); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (14);

            enabledBtn.setBounds (r.removeFromTop (36));
            r.removeFromTop (10);

            auto sliderRow = [&] (juce::Label& lab, juce::Slider& sl)
            {
                auto row = r.removeFromTop (30);
                lab.setBounds (row.removeFromLeft (80));
                sl .setBounds (row);
                r.removeFromTop (8);
            };
            sliderRow (threshLabel,  threshSlider);
            sliderRow (attackLabel,  attackSlider);
            sliderRow (releaseLabel, releaseSlider);
            sliderRow (holdLabel,    holdSlider);

            r.removeFromTop (8);
            helpLabel.setBounds (r);
        }

        void timerCallback() override
        {
            // Resync if a preset / scene recall changed the values out from
            // under us. Only mutate the UI if the live value differs to
            // avoid stomping mid-drag interaction.
            auto& g = App::get().getAudioEngine().getGate();
            if (! threshSlider .isMouseButtonDown() && std::abs ((float) threshSlider .getValue() - g.getThresholdDb()) > 0.01f)
                threshSlider .setValue (g.getThresholdDb(), juce::dontSendNotification);
            if (! attackSlider .isMouseButtonDown() && std::abs ((float) attackSlider .getValue() - g.getAttackMs())    > 0.01f)
                attackSlider .setValue (g.getAttackMs(),    juce::dontSendNotification);
            if (! releaseSlider.isMouseButtonDown() && std::abs ((float) releaseSlider.getValue() - g.getReleaseMs())   > 0.01f)
                releaseSlider.setValue (g.getReleaseMs(),   juce::dontSendNotification);
            if (! holdSlider   .isMouseButtonDown() && std::abs ((float) holdSlider   .getValue() - g.getHoldMs())      > 0.01f)
                holdSlider   .setValue (g.getHoldMs(),      juce::dontSendNotification);
            if (enabledBtn.getToggleState() != g.isEnabled())
            {
                enabledBtn.setToggleState (g.isEnabled(), juce::dontSendNotification);
                enabledBtn.setButtonText (g.isEnabled() ? "GATE: ON" : "GATE: OFF");
            }
        }

    private:
        juce::TextButton enabledBtn;
        juce::Slider     threshSlider, attackSlider, releaseSlider, holdSlider;
        juce::Label      threshLabel,  attackLabel,  releaseLabel,  holdLabel;
        juce::Label      helpLabel;
    };
}

void showNoiseGateDialog()
{
    toolRegistry().showOrFocus ("noiseGate", "Noise Gate",
        [] () -> juce::Component* { return new NoiseGatePanel(); });
}

//==============================================================================
// Offline render dialog
//==============================================================================
} // namespace ns::Dialogs

#include "../../Core/OfflineRenderer.h"

namespace ns::Dialogs
{
namespace
{
    // Lightweight session-persistence for the offline render dialog so the
    // user doesn't have to re-pick everything each time they open it.
    struct OfflineRenderSettings
    {
        juce::File   lastInput;
        juce::File   lastOutput;
        juce::String lastBase = "NeuralStage_Render";
        int          lastMask = 0xF;     // all 4 stems
        int          lastBits = 24;
    };
    static OfflineRenderSettings& renderSettings()
    {
        static OfflineRenderSettings s;
        return s;
    }

    class OfflineRenderPanel : public juce::Component
    {
    public:
        OfflineRenderPanel()
        {
            header.setText ("OFFLINE RENDER  --  bounce an input WAV through the live rig",
                            juce::dontSendNotification);
            header.setJustificationType (juce::Justification::centred);
            header.setColour (juce::Label::textColourId, ns::Colours::accentGlow);
            header.setFont (juce::Font (juce::FontOptions (13.5f).withStyle ("Bold")));
            addAndMakeVisible (header);

            hint.setText ("Pick a guitar DI WAV (mono or stereo). The renderer will bounce it "
                          "through the CURRENT rig faster than real-time and write the selected "
                          "stems to the output folder. Your live audio device is stopped during "
                          "the render and restarted automatically when it finishes.",
                          juce::dontSendNotification);
            hint.setJustificationType (juce::Justification::topLeft);
            hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
            hint.setFont (juce::Font (juce::FontOptions (11.0f)));
            addAndMakeVisible (hint);

            inputBtn .setButtonText ("Input WAV...");
            inputBtn .onClick = [this] { pickInput(); };
            outputBtn.setButtonText ("Output Folder...");
            outputBtn.onClick = [this] { pickOutput(); };
            addAndMakeVisible (inputBtn);
            addAndMakeVisible (outputBtn);

            for (auto* l : { &inputLabel, &outputLabel, &baseLabel, &bitsLabel })
            {
                l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
                l->setFont (juce::Font (juce::FontOptions (12.0f)));
                addAndMakeVisible (*l);
            }
            inputLabel .setText ("(no file)",   juce::dontSendNotification);
            outputLabel.setText ("(no folder)", juce::dontSendNotification);
            baseLabel  .setText ("Base name:",  juce::dontSendNotification);
            bitsLabel  .setText ("Format:",     juce::dontSendNotification);

            // Restore last-used input/output if still on disk.
            if (renderSettings().lastInput.existsAsFile())
            {
                inputFile = renderSettings().lastInput;
                inputLabel.setText (inputFile.getFullPathName(), juce::dontSendNotification);
            }
            if (renderSettings().lastOutput.isDirectory())
            {
                outputFile = renderSettings().lastOutput;
                outputLabel.setText (outputFile.getFullPathName(), juce::dontSendNotification);
            }

            baseName.setText (renderSettings().lastBase, juce::dontSendNotification);
            baseName.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.4f));
            baseName.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            addAndMakeVisible (baseName);

            bitsBox.addItem ("16-bit PCM", 16);
            bitsBox.addItem ("24-bit PCM", 24);
            bitsBox.addItem ("32-bit float", 32);
            bitsBox.setSelectedId (renderSettings().lastBits, juce::dontSendNotification);
            addAndMakeVisible (bitsBox);

            for (int i = 0; i < 4; ++i)
            {
                stemToggles[i].setButtonText (kStemLabels[i]);
                stemToggles[i].setToggleState ((renderSettings().lastMask & kStemFlags[i]) != 0,
                                               juce::dontSendNotification);
                stemToggles[i].setColour (juce::ToggleButton::textColourId, juce::Colours::white);
                addAndMakeVisible (stemToggles[i]);
            }

            renderBtn.setButtonText ("Render");
            renderBtn.onClick = [this] { startRender(); };
            closeBtn .setButtonText ("Close");
            closeBtn.onClick = [this]
            {
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (0);
            };
            addAndMakeVisible (renderBtn);
            addAndMakeVisible (closeBtn);

            setSize (560, 380);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (ns::Colours::background);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (14);
            header.setBounds (r.removeFromTop (26));
            r.removeFromTop (6);
            hint  .setBounds (r.removeFromTop (60));
            r.removeFromTop (8);

            auto rowIn  = r.removeFromTop (28);
            inputBtn .setBounds (rowIn.removeFromLeft (140));
            rowIn.removeFromLeft (8);
            inputLabel.setBounds (rowIn);
            r.removeFromTop (4);

            auto rowOut = r.removeFromTop (28);
            outputBtn.setBounds (rowOut.removeFromLeft (140));
            rowOut.removeFromLeft (8);
            outputLabel.setBounds (rowOut);
            r.removeFromTop (8);

            auto rowBase = r.removeFromTop (28);
            baseLabel.setBounds (rowBase.removeFromLeft (80));
            baseName .setBounds (rowBase.removeFromLeft (260));
            rowBase.removeFromLeft (12);
            bitsLabel.setBounds (rowBase.removeFromLeft (52));
            bitsBox  .setBounds (rowBase);
            r.removeFromTop (10);

            auto stemsRow = r.removeFromTop (28);
            const int cellW = stemsRow.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
                stemToggles[i].setBounds (stemsRow.removeFromLeft (cellW));
            r.removeFromTop (12);

            auto buttons = r.removeFromBottom (32);
            closeBtn .setBounds (buttons.removeFromRight (96));
            buttons.removeFromRight (8);
            renderBtn.setBounds (buttons.removeFromRight (120));
        }

    private:
        static constexpr const char* kStemLabels[4] = { "DI", "Post-NAM", "Post-IR", "Master" };
        static constexpr int          kStemFlags[4]  = { OfflineRenderer::StemDI,
                                                         OfflineRenderer::StemPostNAM,
                                                         OfflineRenderer::StemPostIR,
                                                         OfflineRenderer::StemMaster };

        void pickInput()
        {
            inputChooser = std::make_unique<juce::FileChooser> (
                "Pick a guitar DI WAV", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            inputChooser->launchAsync (juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f.existsAsFile())
                    {
                        inputFile = f;
                        inputLabel.setText (f.getFullPathName(), juce::dontSendNotification);
                        if (! outputFile.isDirectory())
                            setDefaultOutput (f.getParentDirectory());
                        baseName.setText (f.getFileNameWithoutExtension() + "_Rig",
                                          juce::dontSendNotification);
                    }
                });
        }

        void pickOutput()
        {
            outputChooser = std::make_unique<juce::FileChooser> (
                "Pick an output folder",
                outputFile.isDirectory() ? outputFile
                                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            outputChooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectDirectories,
                [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f.isDirectory()) setDefaultOutput (f);
                });
        }

        void setDefaultOutput (const juce::File& f)
        {
            outputFile = f;
            outputLabel.setText (f.getFullPathName(), juce::dontSendNotification);
        }

        void startRender()
        {
            if (! inputFile.existsAsFile())
            {
                ns::ThemedAlerts::showWarning ("Offline Render", "Pick an input WAV first.");
                return;
            }
            if (! outputFile.isDirectory())
            {
                ns::ThemedAlerts::showWarning ("Offline Render", "Pick an output folder first.");
                return;
            }
            int mask = 0;
            for (int i = 0; i < 4; ++i)
                if (stemToggles[i].getToggleState()) mask |= kStemFlags[i];
            if (mask == 0)
            {
                ns::ThemedAlerts::showWarning ("Offline Render", "Select at least one stem to render.");
                return;
            }

            const int bits = bitsBox.getSelectedId();
            const auto base = baseName.getText().trim().isNotEmpty()
                              ? baseName.getText().trim()
                              : juce::String ("NeuralStage_Render");

            OfflineRenderer renderer (App::get().getAudioEngine(),
                                      inputFile, outputFile, base, mask, bits);
            renderer.runThread();   // blocks with a progress window

            // Persist for next time.
            renderSettings().lastInput  = inputFile;
            renderSettings().lastOutput = outputFile;
            renderSettings().lastBase   = base;
            renderSettings().lastMask   = mask;
            renderSettings().lastBits   = bits;

            if (renderer.wasSuccessful())
            {
                ns::ThemedAlerts::showQuestion (
                    "Render Complete",
                    renderer.getResultMessage(),
                    "Open Folder", "OK",
                    [folder = outputFile] (bool open)
                    {
                        if (open) folder.revealToUser();
                    });
            }
            else
            {
                ns::ThemedAlerts::showWarning ("Render Failed", renderer.getResultMessage());
            }
        }

        juce::Label      header, hint;
        juce::TextButton inputBtn, outputBtn, renderBtn, closeBtn;
        juce::Label      inputLabel, outputLabel, baseLabel, bitsLabel;
        juce::TextEditor baseName;
        juce::ComboBox   bitsBox;
        juce::ToggleButton stemToggles[4];
        juce::File       inputFile, outputFile;
        std::unique_ptr<juce::FileChooser> inputChooser, outputChooser;
    };
} // anonymous

void showOfflineRender()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle                  = "Offline Render";
    o.dialogBackgroundColour       = ns::Colours::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar            = false;
    o.resizable                    = false;
    o.componentToCentreAround      = getAppMainWindow();
    o.content.setOwned (new OfflineRenderPanel());
    if (auto* dw = o.launchAsync())
        dw->setLookAndFeel (&ns::appWindowLNF());
}

} // namespace ns::Dialogs
