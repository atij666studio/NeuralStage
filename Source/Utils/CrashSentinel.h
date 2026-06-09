#pragma once
#include <juce_core/juce_core.h>
#include "FileUtils.h"

namespace ns
{
    /** Drops a small flag file on startup and removes it on clean shutdown.
     *  If the flag is still on disk at the next startup, the previous run
     *  ended abnormally (crash, power loss, force-quit). The app can then
     *  offer to restore the auto-saved session.
     *
     *  We deliberately keep the autosaved state files (LastChain.nschain,
     *  LastScenes.xml, LastPreChain.nschain, LastNamSlots.xml) on disk
     *  regardless — they are the source of truth for normal startup too,
     *  and the timer in App writes them every 30 s. The sentinel only
     *  changes the USER PROMPT, not the data.
     */
    struct CrashSentinel
    {
        static juce::File flagFile()
        {
            return FileUtils::userDataDir().getChildFile ("LiveSession.flag");
        }

        /** True if the flag was present at startup (i.e. previous run crashed).
         *  Snapshot the result via wasCrashDetected() — call detectAndArm()
         *  ONCE early in initialise() before writing the new flag. */
        static bool detectAndArm()
        {
            crashedLast = flagFile().existsAsFile();
            armNew();
            return crashedLast;
        }

        static bool wasCrashDetected() noexcept { return crashedLast; }

        /** Call from App::shutdown() and systemRequestedQuit() so a clean
         *  exit removes the flag. */
        static void disarm()
        {
            auto f = flagFile();
            if (f.existsAsFile()) f.deleteFile();
        }

    private:
        inline static bool crashedLast { false };

        static void armNew()
        {
            auto f = flagFile();
            f.replaceWithText (juce::String ("time=") + juce::Time::getCurrentTime().toISO8601 (true)
                                + " machine=" + juce::SystemStats::getComputerName());
        }
    };
} // namespace ns
