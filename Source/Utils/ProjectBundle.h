#pragma once
#include <juce_core/juce_core.h>
#include "FileUtils.h"

namespace ns
{
    /** A .nsproject bundle is a plain .zip that captures everything needed to
     *  reproduce a live-rig setup on another machine: the autosaved chain
     *  files, the scene bank, NAM slot path list, tempo/clock/morph prefs,
     *  and window state.
     *
     *  We deliberately store ONLY the small state files — NAM/IR audio
     *  binaries are referenced by absolute path inside LastNamSlots.xml.
     *  A future enhancement could optionally embed the model/IR binaries.
     */
    struct ProjectBundle
    {
        static constexpr const char* fileExtension = "nsproject";

        /** Writes a .nsproject bundle from the current userDataDir state.
         *  Returns true on success. */
        static bool exportTo (const juce::File& dest)
        {
            const auto root = FileUtils::userDataDir();
            juce::ZipFile::Builder zb;

            auto addIfExists = [&] (const juce::File& f, const juce::String& storedName)
            {
                if (f.existsAsFile()) zb.addFile (f, 9, storedName);
            };

            addIfExists (root.getChildFile ("LastChain.nschain"),    "LastChain.nschain");
            addIfExists (root.getChildFile ("LastPreChain.nschain"), "LastPreChain.nschain");
            addIfExists (root.getChildFile ("LastScenes.xml"),       "LastScenes.xml");
            addIfExists (root.getChildFile ("LastNamSlots.xml"),     "LastNamSlots.xml");
            addIfExists (root.getChildFile ("LastTempo.txt"),        "LastTempo.txt");
            addIfExists (root.getChildFile ("MidiClock.txt"),        "MidiClock.txt");
            addIfExists (root.getChildFile ("SceneMorph.txt"),       "SceneMorph.txt");
            addIfExists (root.getChildFile ("WindowState.txt"),      "WindowState.txt");
            addIfExists (FileUtils::midiAssignmentsFile(),           "MidiAssignments.xml");
            addIfExists (FileUtils::audioDeviceSettingsFile(),       "AudioDeviceSettings.xml");

            // Bundle marker so we can validate on import.
            {
                juce::MemoryBlock mb;
                {
                    juce::MemoryOutputStream out (mb, false);
                    out << "NeuralStageProjectBundle v1\n"
                        << "exported=" << juce::Time::getCurrentTime().toISO8601 (true) << "\n";
                }
                zb.addEntry (new juce::MemoryInputStream (mb, true),
                             9, "bundle.info",
                             juce::Time::getCurrentTime());
            }

            if (dest.existsAsFile()) dest.deleteFile();
            juce::FileOutputStream out (dest);
            if (! out.openedOk()) return false;
            double progress = 0.0;
            const bool ok = zb.writeToStream (out, &progress);
            out.flush();
            return ok;
        }

        /** Extracts a .nsproject bundle over the current userDataDir state.
         *  Existing files with the same name are OVERWRITTEN. The caller is
         *  responsible for triggering a state reload afterwards (typically
         *  by restarting the app, since chain/scene reload paths run only
         *  in App::initialise). Returns true if extraction succeeded and
         *  the bundle.info marker validated. */
        static bool importFrom (const juce::File& src)
        {
            if (! src.existsAsFile()) return false;
            juce::ZipFile zf (src);
            // Validate the marker.
            bool hasMarker = false;
            for (int i = 0; i < zf.getNumEntries(); ++i)
                if (zf.getEntry (i)->filename == "bundle.info") { hasMarker = true; break; }
            if (! hasMarker) return false;

            const auto root = FileUtils::userDataDir();
            root.createDirectory();
            const auto res = zf.uncompressTo (root, /*shouldOverwrite*/ true);
            return res.wasOk();
        }
    };
} // namespace ns
