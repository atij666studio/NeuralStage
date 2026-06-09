#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace ns::Dialogs
{
    /** Pops up the standard JUCE Audio/MIDI device picker as a modal dialog. */
    void showAudioMidiSettings();

    /** Preset browser: lists ~/Documents/NeuralStage/Presets, with Save/Save As/Load/Delete. */
    void showPresetBrowser();

    /** MIDI assignments table: lists current learned mappings, allows clear / clear all. */
    void showMidiAssignments();

    /** Footswitch wizard: curated guided UI for binding the most common live
     *  footswitch targets (tap tempo, A/B compare, scene recalls, block
     *  bypasses, master / input mute) to MIDI in one place, one click per
     *  binding. Simpler than the full MIDI Assignments table. */
    void showFootswitchWizard();

    /** Offline render dialog: pick an input WAV, choose which stems to write
     *  (DI / Post-NAM / Post-IR / Master), and bounce through the live rig
     *  faster-than-real-time. */
    void showOfflineRender();

    /** "About NeuralStage" — splash-style modal with version info. */
    void showAboutDialog();

    /** Looper transport panel: REC / PLAY / STOP / CLEAR + level + status. */
    void showLooperDialog();
    void showBackingTrackDialog();

    /** Noise gate settings: threshold / attack / release / hold + on/off.
     *  This is the post-FX safety gate that runs after the user's plugin
     *  chain -- complementary to any third-party gate plugin a user has
     *  loaded into the FX chain. */
    void showNoiseGateDialog();

    /** Opens the user manual in the OS default viewer.
     *  Prefers Docs\NeuralStage-Manual.pdf next to the executable; falls
     *  back to the bundled .md if the PDF is missing. */
    void openManual();
}
