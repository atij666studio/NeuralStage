#pragma once
#include <juce_core/juce_core.h>
#include "FileUtils.h"

namespace ns
{
    /** Installs a FileLogger so juce::Logger::writeToLog() goes to a rotating
     *  log file in the user-data dir. Auto-rotates by date-stamp and trims
     *  the directory to a small number of recent logs.
     *
     *  Usage:
     *      ns::AppLogger::install ("NeuralStage v0.2.0 starting...");
     *      // ... runtime ...
     *      ns::AppLogger::uninstall();
     */
    struct AppLogger
    {
        static void install (const juce::String& welcomeMessage)
        {
            // Ensure the directory exists (the global ensureCreated below is
            // belt-and-braces; FileUtils::userDataDir().createDirectory() is
            // also called from App::initialise()).
            const auto dir = FileUtils::userDataDir().getChildFile ("Logs");
            dir.createDirectory();

            // Keep at most 10 recent logs (oldest get pruned).
            const auto maxInitialFileSizeBytes = 8 * 1024 * 1024; // 8 MB cap on single startup log
            auto* logger = juce::FileLogger::createDateStampedLogger (
                dir.getFullPathName(),
                "NeuralStage_",
                ".log",
                welcomeMessage);
            juce::Logger::setCurrentLogger (logger);
            current = logger;

            pruneOldLogs (dir, 10);

            juce::ignoreUnused (maxInitialFileSizeBytes);
        }

        static void uninstall()
        {
            juce::Logger::setCurrentLogger (nullptr);
            delete current;
            current = nullptr;
        }

        static juce::File currentLogFile()
        {
            if (current != nullptr) return current->getLogFile();
            return {};
        }

    private:
        inline static juce::FileLogger* current { nullptr };

        static void pruneOldLogs (const juce::File& dir, int keepNewest)
        {
            auto files = dir.findChildFiles (juce::File::findFiles, false, "NeuralStage_*.log");
            if (files.size() <= keepNewest) return;
            // Sort by modification time, oldest first.
            std::sort (files.begin(), files.end(),
                       [] (const juce::File& a, const juce::File& b)
                       {
                           return a.getLastModificationTime() < b.getLastModificationTime();
                       });
            const int toDelete = files.size() - keepNewest;
            for (int i = 0; i < toDelete; ++i)
                files[i].deleteFile();
        }
    };
} // namespace ns
