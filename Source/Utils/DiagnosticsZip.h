#pragma once
#include <juce_core/juce_core.h>
#include "FileUtils.h"
#include "AppLogger.h"

namespace ns
{
    /** Bundles the most-recently relevant diagnostic files into a single .zip
     *  on the user's Desktop. Intended for "Report a Bug" — the user can
     *  attach the resulting zip to an email or bug tracker.
     *
     *  Includes (best-effort, missing files are silently skipped):
     *    - The current NeuralStage log file
     *    - DeadMansPedal.txt   (blacklist of crashy plugins)
     *    - LastChain.nschain   (post-FX chain)
     *    - LastPreChain.nschain
     *    - LastScenes.xml
     *    - LastNamSlots.xml
     *    - AudioDeviceSettings.xml
     *    - MidiAssignments.xml
     *    - MidiClock.txt, SceneMorph.txt, WindowState.txt, LastTempo.txt
     *
     *  Returns the resulting zip file (or an empty File on failure).
     */
    struct DiagnosticsZip
    {
        static juce::File buildOnDesktop (const juce::String& appVersion,
                                          const juce::String& extraNote = {})
        {
            const auto dest = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                .getChildFile ("NeuralStage-Diagnostics-"
                                               + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S")
                                               + ".zip");

            juce::ZipFile::Builder zb;
            const auto root = FileUtils::userDataDir();

            auto addIfExists = [&] (const juce::File& f, const juce::String& storedName)
            {
                if (f.existsAsFile()) zb.addFile (f, 9, storedName);
            };

            // Logs
            if (auto log = AppLogger::currentLogFile(); log.existsAsFile())
                zb.addFile (log, 9, "Logs/" + log.getFileName());

            // Recent rotated logs (up to 3 newest besides the current one)
            {
                auto logDir = root.getChildFile ("Logs");
                if (logDir.isDirectory())
                {
                    auto files = logDir.findChildFiles (juce::File::findFiles, false, "NeuralStage_*.log");
                    std::sort (files.begin(), files.end(),
                               [] (const juce::File& a, const juce::File& b)
                               {
                                   return a.getLastModificationTime() > b.getLastModificationTime();
                               });
                    const int n = juce::jmin (4, files.size());
                    for (int i = 0; i < n; ++i)
                        zb.addFile (files[i], 9, "Logs/" + files[i].getFileName());
                }
            }

            addIfExists (root.getChildFile ("DeadMansPedal.txt"),       "DeadMansPedal.txt");
            addIfExists (root.getChildFile ("LastChain.nschain"),       "State/LastChain.nschain");
            addIfExists (root.getChildFile ("LastPreChain.nschain"),    "State/LastPreChain.nschain");
            addIfExists (root.getChildFile ("LastScenes.xml"),          "State/LastScenes.xml");
            addIfExists (root.getChildFile ("LastNamSlots.xml"),        "State/LastNamSlots.xml");
            addIfExists (FileUtils::audioDeviceSettingsFile(),          "State/AudioDeviceSettings.xml");
            addIfExists (FileUtils::midiAssignmentsFile(),              "State/MidiAssignments.xml");
            addIfExists (root.getChildFile ("MidiClock.txt"),           "State/MidiClock.txt");
            addIfExists (root.getChildFile ("SceneMorph.txt"),          "State/SceneMorph.txt");
            addIfExists (root.getChildFile ("WindowState.txt"),         "State/WindowState.txt");
            addIfExists (root.getChildFile ("LastTempo.txt"),           "State/LastTempo.txt");

            // System summary
            {
                juce::MemoryBlock mb;
                {
                    juce::MemoryOutputStream out (mb, false);
                    out << "NeuralStage Diagnostics\n"
                        << "=======================\n"
                        << "App version: "  << appVersion << "\n"
                        << "Build time:  "  << juce::String (__DATE__) << " " << juce::String (__TIME__) << "\n"
                        << "Captured:    "  << juce::Time::getCurrentTime().toString (true, true) << "\n"
                        << "OS:          "  << juce::SystemStats::getOperatingSystemName() << "\n"
                        << "CPU:         "  << juce::SystemStats::getCpuVendor()
                                            << " (" << juce::SystemStats::getNumCpus() << " cores, "
                                            << juce::SystemStats::getCpuSpeedInMegahertz() << " MHz)\n"
                        << "RAM:         "  << juce::SystemStats::getMemorySizeInMegabytes() << " MB\n"
                        << "User:        "  << juce::SystemStats::getLogonName() << "\n";
                    if (extraNote.isNotEmpty())
                        out << "\nUser note:\n" << extraNote << "\n";
                }
                zb.addEntry (new juce::MemoryInputStream (mb, true),
                             9, "SystemInfo.txt",
                             juce::Time::getCurrentTime());
            }

            if (dest.existsAsFile()) dest.deleteFile();
            juce::FileOutputStream out (dest);
            if (! out.openedOk()) return {};

            double progress = 0.0;
            const bool ok = zb.writeToStream (out, &progress);
            out.flush();
            if (! ok) { dest.deleteFile(); return {}; }
            return dest;
        }
    };
} // namespace ns
