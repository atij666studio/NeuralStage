#include "PluginManager.h"
#include "PluginManagerPlatform.h"
#include "../Utils/FileUtils.h"
#include "../Utils/SystemModal.h"

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    // ---------------------------------------------------------------
    // GUARDIAN SUBPROCESS
    //
    // Started once at app launch. We pass it our PID + the .app bundle
    // path on its argv. The guardian opens a kqueue NOTE_EXIT watcher on
    // our PID and blocks until we die. When we die, IF a marker file
    // exists at /tmp/NeuralStage.scanning.<pid>, it relaunches the app
    // via `/usr/bin/open <bundle>` — then itself exits.
    //
    // The marker is created BEFORE each scanNextFile() and removed
    // AFTER it returns. So the guardian only relaunches us when we
    // died inside a plugin load (iLok Quit, codesign SIGKILL, etc.).
    // Clean user quit → no marker → no relaunch.
    //
    // Works against any death mode: SIGKILL, abort(), _exit(), exit(),
    // hard crash. Does NOT depend on atexit fired correctly.
    // ---------------------------------------------------------------

    std::atomic<bool> g_scanInProgress { false };
    juce::String      g_appBundlePath;
    int               g_guardianPid { 0 };

    static juce::File markerFileForPid (int pid)
    {
        return juce::File (juce::File::getSpecialLocation (juce::File::tempDirectory))
                 .getChildFile ("NeuralStage.scanning." + juce::String (pid));
    }

    // Find the .app bundle (macOS) / executable (Windows + Linux) for relaunch.
    static juce::String discoverBundlePath()
    {
        return ns::plat::getAppLaunchTarget();
    }

    // Internal entry point invoked when the running binary is launched
    // with `--ns-guardian <pid> <bundle>`. Watches PID via the platform
    // layer and relaunches the bundle if a marker file exists at the
    // time of death.
    static int runGuardian (int argc, const char* const* argv)
    {
        if (argc < 4) return 0;
        const int target = juce::String (argv[2]).getIntValue();
        juce::String bundle (argv[3]);
        if (target <= 0 || bundle.isEmpty()) return 0;

        ns::plat::waitForProcessExit (target);

        // Did the parent die during a scan? Marker file presence tells us.
        auto marker = markerFileForPid (target);
        if (! marker.existsAsFile())
            return 0;
        marker.deleteFile();

        // Give the OS a moment to fully reap the parent before we relaunch.
        ns::plat::sleepMs (300);
        ns::plat::relaunchApp (bundle);
        return 0;
    }

    // Spawn the guardian once. Idempotent.
    static void ensureGuardianStarted()
    {
        if (g_guardianPid != 0) return;
        g_appBundlePath = discoverBundlePath();
        if (g_appBundlePath.isEmpty()) return;

        const juce::String selfExe = ns::plat::getExecutablePath();
        if (selfExe.isEmpty()) return;

        g_guardianPid = ns::plat::launchGuardianChild (selfExe,
                                                       ns::plat::getProcessId(),
                                                       g_appBundlePath);
    }

    static void markScanStart (const juce::String& nextFile)
    {
        ensureGuardianStarted();
        g_scanInProgress.store (true);
        auto m = markerFileForPid (ns::plat::getProcessId());
        m.replaceWithText (nextFile + "\n");
    }

    static void markScanEnd()
    {
        g_scanInProgress.store (false);
        markerFileForPid (ns::plat::getProcessId()).deleteFile();
    }
}

// Public hook: called from main() before JUCE starts. If argv contains
// --ns-guardian, we run as the guardian and never start the app.
int neuralstage_maybe_run_guardian (int argc, const char* const* argv)
{
    if (argc >= 2 && juce::String (argv[1]) == "--ns-guardian")
    {
        runGuardian (argc, argv);
        return 1; // exit main
    }
    return 0;
}

//==============================================================================
// SUBPROCESS PLUGIN SCAN MODE
//
// Re-execs ourselves with `--ns-scan-plugin <formatName> <fileIdentifier>`.
// Child:
//   1. Initialises an AudioPluginFormatManager (no UI, no audio).
//   2. Calls format->findAllTypesForFile() on the given identifier.
//   3. Writes resulting PluginDescription XMLs to stdout (one root tag).
//   4. _exit(0) on success.
// If the plugin crashes / hangs / shows iLok Quit dialog → child dies.
// Parent:
//   * Detects non-zero exit OR timeout → blacklist the file in the pedal.
//   * On success → parses stdout XML, adds each PluginDescription to
//     KnownPluginList, persists, moves on.
// Either way the host process never dies and never re-runs the scan.
//==============================================================================
int neuralstage_maybe_run_plugin_scan (int argc, const char* const* argv)
{
    if (argc < 4 || juce::String (argv[1]) != "--ns-scan-plugin")
        return 0;

   #if JUCE_WINDOWS
    // Suppress Windows critical-error / GP-fault / file-not-found message
    // boxes so eLicenser / SynsoAcc / iLok protected plugins can't pop
    // modal dialogs that block our scan child indefinitely.
    ::SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
   #endif

    juce::ScopedJuceInitialiser_GUI guiInit;

    juce::String formatName (argv[2]);
    juce::String fileId    (argv[3]);

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();

    juce::AudioPluginFormat* fmt = nullptr;
    for (int i = 0; i < fm.getNumFormats(); ++i)
        if (fm.getFormat (i)->getName() == formatName)
        {
            fmt = fm.getFormat (i);
            break;
        }
    if (fmt == nullptr) ns::plat::exitImmediate (2);

    juce::OwnedArray<juce::PluginDescription> descs;
    fmt->findAllTypesForFile (descs, fileId);
    if (descs.isEmpty()) ns::plat::exitImmediate (3);

    juce::XmlElement root ("ScanResult");
    for (auto* d : descs)
        root.addChildElement (d->createXml().release());

    auto xml = root.toString (juce::XmlElement::TextFormat().singleLine());
    std::fwrite (xml.toRawUTF8(), 1, (size_t) xml.getNumBytesAsUTF8(), stdout);
    std::fflush (stdout);
    ns::plat::exitImmediate (0);
}

//==============================================================================
// SUBPROCESS PLUGIN VALIDATE MODE
//
// Re-execs ourselves with `--ns-validate-plugin <descXmlFile> <sampleRate>
// <blockSize>`. The child:
//   1. Reads PluginDescription XML from the file
//   2. Calls formatManager.createPluginInstance()
//   3. Calls setPlayConfigDetails + prepareToPlay
//   4. Calls releaseResources
//   5. _exit(0) on success
// If the plugin crashes / SIGKILLs / hangs → child dies.
// Used as a pre-flight before adding a plugin to the chain so a buggy
// plugin can never take down the host.
//==============================================================================
int neuralstage_maybe_run_plugin_validate (int argc, const char* const* argv)
{
    if (argc < 5 || juce::String (argv[1]) != "--ns-validate-plugin")
        return 0;

   #if JUCE_WINDOWS
    ::SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
   #endif

    juce::ScopedJuceInitialiser_GUI guiInit;

    const juce::File descFile { juce::String (argv[2]) };
    const double sampleRate = juce::String (argv[3]).getDoubleValue();
    const int    blockSize  = juce::String (argv[4]).getIntValue();

    auto xml = juce::XmlDocument (descFile).getDocumentElement();
    if (xml == nullptr) ns::plat::exitImmediate (10);

    juce::PluginDescription desc;
    if (! desc.loadFromXml (*xml)) ns::plat::exitImmediate (11);

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();

    juce::String err;
    auto inst = fm.createPluginInstance (desc,
                                         sampleRate > 0 ? sampleRate : 48000.0,
                                         blockSize  > 0 ? blockSize  : 512,
                                         err);
    if (inst == nullptr) ns::plat::exitImmediate (12);

    inst->setPlayConfigDetails (2, 2,
                                sampleRate > 0 ? sampleRate : 48000.0,
                                blockSize  > 0 ? blockSize  : 512);
    inst->prepareToPlay (sampleRate > 0 ? sampleRate : 48000.0,
                         blockSize  > 0 ? blockSize  : 512);
    inst->releaseResources();
    inst.reset();
    ns::plat::exitImmediate (0);
}

//==============================================================================
class PluginManager::IncrementalScanner : private juce::Timer
{
public:
    IncrementalScanner (PluginManager& o,
                        std::function<void (const juce::String&)> p,
                        std::function<void ()> d)
        : owner (o), progressCb (std::move (p)), doneCb (std::move (d))
    {
        ns::FileUtils::userDataDir().createDirectory();

        // Locate our own executable so we can re-exec it in --ns-scan-plugin
        // mode for each plugin we want to validate.
        selfExePath = ns::plat::getExecutablePath();

        // Snapshot what's in the known list NOW (before any background work)
        // and stick it in a hash set. The hot loop only does set lookups —
        // no per-entry walking through KnownPluginList::getTypes().
        for (const auto& d : owner.knownPlugins.getTypes())
            knownIdSet.insert (d.fileOrIdentifier.toStdString());

        // Cache the dead-mans-pedal once.
        const auto pedal = getDeadMansPedalFile();
        if (pedal.existsAsFile())
        {
            const auto lines = juce::StringArray::fromLines (pedal.loadFileAsString());
            for (const auto& s : lines)
                if (s.isNotEmpty())
                    blacklistSet.insert (s.toStdString());
        }

        // Load the "needs authentication" list. These are plugins whose
        // child-process scan TIMED OUT on a previous launch — almost
        // always iLok/PACE/Waves shell type plugins that block on a
        // license dialog. We DON'T blacklist them (so they're retried
        // each launch in case the user authenticates) but we DO skip
        // them this session after a timeout to avoid blocking the scan
        // for 30 s × N plugins.
        const auto authFile = getNeedsAuthFile();
        if (authFile.existsAsFile())
        {
            const auto lines = juce::StringArray::fromLines (authFile.loadFileAsString());
            for (const auto& s : lines)
                if (s.isNotEmpty())
                    previouslyNeededAuthSet.insert (s.toStdString());
        }

        // Kick off enumeration on a background thread. The UI overlay
        // appears immediately; the timer will spin pulling completed
        // entries from the queue as they arrive.
        startEnumerationThread();

        startTimer (10);
    }

    ~IncrementalScanner() override
    {
        stopTimer();
        cancelEnumeration.store (true);
        if (enumThread.joinable())
            enumThread.join();

        // Kill any subprocesses still in flight (cancel / quit during scan).
        for (auto& f : inflight)
            if (f.cp != nullptr)
                f.cp->kill();
        inflight.clear();
    }

    static juce::File getDeadMansPedalFile()
    {
        return ns::FileUtils::userDataDir().getChildFile ("DeadMansPedal.txt");
    }

    static juce::File getNeedsAuthFile()
    {
        return ns::FileUtils::userDataDir().getChildFile ("NeedsAuth.txt");
    }

private:
    struct Entry { juce::String formatName, fileId; };

    void startEnumerationThread()
    {
        // Build the (formatName, defaultLocations) pairs on the message
        // thread BEFORE detaching, since AudioPluginFormat* lifetime is
        // tied to the manager (UI thread).
        struct FormatInfo { juce::String name; juce::FileSearchPath paths; juce::AudioPluginFormat* fmt; };
        std::vector<FormatInfo> formats;
        for (int i = 0; i < owner.formatManager.getNumFormats(); ++i)
        {
            auto* fmt = owner.formatManager.getFormat (i);
            if (fmt == nullptr) continue;
            formats.push_back ({ fmt->getName(), fmt->getDefaultLocationsToSearch(), fmt });
        }

        enumThread = std::thread ([this, formats = std::move (formats)]
        {
            int approxTotal = 0;
            for (const auto& fi : formats)
            {
                if (cancelEnumeration.load()) return;

                auto files = fi.fmt->searchPathsForPlugins (fi.paths, true /*recursive*/);

                std::lock_guard<std::mutex> lock (queueMutex);
                for (auto& f : files)
                {
                    if (cancelEnumeration.load()) return;
                    queue.push_back ({ fi.name, f });
                }
                approxTotal += files.size();
                totalCountAtomic.store (approxTotal);
            }
            enumerationDone.store (true);
        });
    }

    void timerCallback() override
    {
        if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
            return;
        if (ns::isSystemModalActive())
        {
            startTimer (200);
            return;
        }
        if (getTimerInterval() != 10) startTimer (10);

        // 1) Harvest any finished/timed-out inflight subprocesses first.
        harvestInflight();

        // 2) Fill the inflight pool up to kMaxConcurrent by pulling fresh
        //    entries off the queue. Bulk-skip known/blacklisted/needs-auth
        //    entries without spawning anything.
        const int maxBatchEnqueued = 2000;
        int processedThisTick = 0;
        while ((int) inflight.size() < kMaxConcurrent
                && processedThisTick++ < maxBatchEnqueued)
        {
            Entry entry;
            {
                std::lock_guard<std::mutex> lock (queueMutex);
                if (cursor >= (int) queue.size())
                {
                    // No more entries — but we may still have inflight
                    // children to wait on.
                    break;
                }
                entry = queue[(size_t) cursor];
                ++cursor;
            }

            const int total = juce::jmax (1, totalCountAtomic.load());
            owner.scanProgress.store ((float) cursor / (float) total);

            const auto idStd = entry.fileId.toStdString();
            if (knownIdSet.find (idStd) != knownIdSet.end())
            {
                if (progressCb) progressCb ("(known) " + entry.fileId);
                continue;
            }
            if (blacklistSet.find (idStd) != blacklistSet.end())
            {
                if (progressCb) progressCb ("(blacklisted) " + entry.fileId);
                continue;
            }
            if (skippedThisSessionSet.find (idStd) != skippedThisSessionSet.end())
            {
                if (progressCb) progressCb ("(needs auth) " + entry.fileId);
                continue;
            }
            // CRITICAL: also skip plugins on disk's NeedsAuth.txt from previous
            // sessions. Without this, every launch re-tries every iLok / PACE /
            // eLicenser plugin and triggers their license-error dialogs all
            // over again. User can force-retry via the right-click "Clear
            // needs-auth list and rescan" menu after authenticating.
            if (previouslyNeededAuthSet.find (idStd) != previouslyNeededAuthSet.end())
            {
                if (progressCb) progressCb ("(needs auth, skipped) " + entry.fileId);
                continue;
            }

            // Need to scan — start a subprocess and add to the inflight pool.
            owner.setCurrentScanText (entry.fileId);
            launchScanSubprocess (entry);
            // Loop continues to fill more slots if the pool isn't full.
        }

        // 3) Done condition: nothing inflight, queue drained, enum done.
        if (inflight.empty())
        {
            std::lock_guard<std::mutex> lock (queueMutex);
            if (cursor >= (int) queue.size() && enumerationDone.load())
            {
                stopTimer();
                owner.saveKnownList();
                owner.scanProgress.store (1.0f);
                owner.setCurrentScanText ({});
                if (doneCb) doneCb();
                return;
            }
        }
    }

    void launchScanSubprocess (const Entry& entry)
    {
        if (selfExePath.isEmpty())
        {
            if (progressCb) progressCb ("(no exe path) " + entry.fileId);
            return;
        }

        auto cp = std::make_unique<juce::ChildProcess>();
        juce::StringArray args { selfExePath,
                                 "--ns-scan-plugin",
                                 entry.formatName,
                                 entry.fileId };
        if (! cp->start (args, juce::ChildProcess::wantStdOut))
        {
            if (progressCb) progressCb ("(spawn failed) " + entry.fileId);
            return;
        }

        Inflight f;
        f.entry   = entry;
        f.cp      = std::move (cp);
        f.startMs = juce::Time::currentTimeMillis();
        inflight.push_back (std::move (f));
    }

    void harvestInflight()
    {
        const auto nowMs = juce::Time::currentTimeMillis();

        for (size_t i = 0; i < inflight.size(); /* manual */)
        {
            auto& f = inflight[i];

            // Non-blocking check: did the child finish?
            // waitForProcessToFinish(0) returns true immediately if finished.
            const bool finished = f.cp->waitForProcessToFinish (0);

            if (! finished)
            {
                // Still running — check for our soft timeout.
                if (nowMs - f.startMs > kPerScanTimeoutMs)
                {
                    f.cp->kill();
                    handleTimedOut (f.entry);
                    inflight.erase (inflight.begin() + (long) i);
                    continue;
                }
                ++i;
                continue;
            }

            // Finished — harvest result, then remove from pool.
            const auto exitCode = f.cp->getExitCode();
            if (exitCode != 0)
            {
                // Genuine crash / hard failure → permanently blacklist.
                blacklist (f.entry.fileId);
                if (progressCb) progressCb ("(exit " + juce::String (exitCode) + ") "
                                            + f.entry.fileId);
            }
            else
            {
                const auto stdoutText = f.cp->readAllProcessOutput();
                if (auto xml = juce::parseXML (stdoutText))
                {
                    int added = 0;
                    for (auto* child : xml->getChildIterator())
                    {
                        juce::PluginDescription pd;
                        if (pd.loadFromXml (*child))
                        {
                            owner.knownPlugins.addType (pd);
                            ++added;
                        }
                    }
                    if (added > 0)
                    {
                        // Success! If this plugin was previously on the
                        // needs-auth list (e.g. user just authorised it),
                        // remove it from that list now.
                        removeFromNeedsAuth (f.entry.fileId);
                        owner.saveKnownList();
                        if (progressCb) progressCb (f.entry.fileId);
                    }
                    else
                    {
                        // Parsed but no descriptions — treat like a soft
                        // failure (don't permanently blacklist, retry
                        // next launch).
                        markNeedsAuth (f.entry.fileId);
                        if (progressCb) progressCb ("(no descriptions) "
                                                    + f.entry.fileId);
                    }
                }
                else
                {
                    markNeedsAuth (f.entry.fileId);
                    if (progressCb) progressCb ("(unparseable output) "
                                                + f.entry.fileId);
                }
            }

            inflight.erase (inflight.begin() + (long) i);
        }
    }

    // Plugin scan timed out — DON'T blacklist (it's almost certainly an
    // iLok/PACE/Waves auth dialog blocking the child). Mark it as
    // needs-auth (retried next launch) and skip it for the rest of THIS
    // scan so it doesn't keep stealing 30 s slots.
    void handleTimedOut (const Entry& entry)
    {
        markNeedsAuth (entry.fileId);
        skippedThisSessionSet.insert (entry.fileId.toStdString());
        if (progressCb) progressCb ("(timeout, needs auth) " + entry.fileId);
    }

    void markNeedsAuth (const juce::String& fileId)
    {
        const auto idStd = fileId.toStdString();
        if (currentlyNeededAuthSet.find (idStd) != currentlyNeededAuthSet.end())
            return;
        currentlyNeededAuthSet.insert (idStd);

        const auto f = getNeedsAuthFile();
        f.getParentDirectory().createDirectory();
        auto lines = f.existsAsFile()
                     ? juce::StringArray::fromLines (f.loadFileAsString())
                     : juce::StringArray{};
        lines.removeEmptyStrings();
        if (! lines.contains (fileId))
            lines.add (fileId);
        f.replaceWithText (lines.joinIntoString ("\n") + "\n");
    }

    void removeFromNeedsAuth (const juce::String& fileId)
    {
        currentlyNeededAuthSet.erase (fileId.toStdString());

        const auto f = getNeedsAuthFile();
        if (! f.existsAsFile()) return;
        auto lines = juce::StringArray::fromLines (f.loadFileAsString());
        lines.removeEmptyStrings();
        lines.removeString (fileId);
        if (lines.isEmpty()) f.deleteFile();
        else                 f.replaceWithText (lines.joinIntoString ("\n") + "\n");
    }

    bool isAlreadyKnown (const juce::String& fileId) const
    {
        return knownIdSet.find (fileId.toStdString()) != knownIdSet.end();
    }

    void blacklist (const juce::String& fileId)
    {
        blacklistSet.insert (fileId.toStdString());

        const auto pedal = getDeadMansPedalFile();
        pedal.getParentDirectory().createDirectory();
        auto lines = pedal.existsAsFile()
                     ? juce::StringArray::fromLines (pedal.loadFileAsString())
                     : juce::StringArray{};
        lines.removeEmptyStrings();
        if (! lines.contains (fileId))
            lines.add (fileId);
        pedal.replaceWithText (lines.joinIntoString ("\n") + "\n");
    }

    PluginManager& owner;
    std::vector<Entry> queue;
    std::mutex         queueMutex;
    int                cursor     { 0 };
    std::atomic<int>   totalCountAtomic { 0 };
    juce::String       selfExePath;
    std::unordered_set<std::string> knownIdSet;
    std::unordered_set<std::string> blacklistSet;
    std::unordered_set<std::string> previouslyNeededAuthSet; // from disk at start
    std::unordered_set<std::string> currentlyNeededAuthSet;  // current file contents
    std::unordered_set<std::string> skippedThisSessionSet;   // timed out this scan
    std::thread        enumThread;
    std::atomic<bool>  cancelEnumeration { false };
    std::atomic<bool>  enumerationDone   { false };
    std::function<void (const juce::String&)> progressCb;
    std::function<void ()> doneCb;

    // Parallel subprocess pool. Per-format scan throughput is roughly
    // linear in this number. 8 keeps total RAM usage reasonable even with
    // large collections; on a 4-core machine it saturates CPU nicely.
    // Previously 4 — doubling gives ~2x throughput on systems with many
    // unknown plugins (all known/blacklisted entries are fast-skipped in
    // the queue loop without spawning anything, so the gain is real).
    static constexpr int           kMaxConcurrent   = 8;
    // 15 s is sufficient for all legitimate plugins on Windows; iLok /
    // PACE / auth-dialog plugins are pre-filtered via the NeedsAuth cache
    // so they never reach a subprocess. 30 s was unnecessarily conservative
    // and meant a single hung plugin stalled a scan slot for half a minute.
    static constexpr juce::int64   kPerScanTimeoutMs = 15000;

    struct Inflight
    {
        Entry                              entry;
        std::unique_ptr<juce::ChildProcess> cp;
        juce::int64                        startMs { 0 };
    };
    std::vector<Inflight> inflight;
};

//==============================================================================
PluginManager::PluginManager()
{
    initialiseFormats();
    loadKnownList();
    handleCrashedAddOnLaunch();
}

PluginManager::~PluginManager() = default;

juce::String PluginManager::getCurrentScanText() const
{
    const juce::ScopedLock sl (scanTextLock);
    return currentScanText;
}

void PluginManager::setCurrentScanText (const juce::String& s)
{
    const juce::ScopedLock sl (scanTextLock);
    currentScanText = s;
}

void PluginManager::initialiseFormats()
{
    formatManager.addDefaultFormats();
}

juce::File PluginManager::getKnownListFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("KnownPlugins.xml");
}

juce::File PluginManager::getDeadMansPedalFile()
{
    return ns::FileUtils::userDataDir().getChildFile ("DeadMansPedal.txt");
}

void PluginManager::loadKnownList()
{
    auto f = getKnownListFile();
    if (! f.existsAsFile()) return;

    if (auto xml = juce::XmlDocument::parse (f))
        knownPlugins.recreateFromXml (*xml);
}

void PluginManager::saveKnownList() const
{
    auto xml = knownPlugins.createXml();
    if (xml == nullptr) return;
    auto f = getKnownListFile();
    f.getParentDirectory().createDirectory();
    xml->writeTo (f, {});
}

void PluginManager::scanFormat (juce::AudioPluginFormat& format,
                                const juce::FileSearchPath& searchPaths,
                                std::function<void (const juce::String&)> progressCb)
{
    juce::PluginDirectoryScanner scanner (knownPlugins, format, searchPaths,
                                          true /*recursive*/, getDeadMansPedalFile());
    juce::String pluginBeingScanned;
    bool more = true;
    while (more)
    {
        try { more = scanner.scanNextFile (true, pluginBeingScanned); }
        catch (...) { more = true; }
        if (progressCb) progressCb (pluginBeingScanned);
    }
    saveKnownList();
}

void PluginManager::scanAllInstalled (std::function<void (const juce::String&)> progressCb)
{
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* fmt = formatManager.getFormat (i);
        if (fmt == nullptr) continue;
        scanFormat (*fmt, fmt->getDefaultLocationsToSearch(), progressCb);
    }
}

void PluginManager::beginAsyncScan (std::function<void (const juce::String&)> progressCb,
                                    std::function<void ()> completionCb)
{
    cancelAsyncScan();
    scanProgress.store (0.0f);
    setCurrentScanText ({});

    // Drop a sentinel so that if we get killed mid-scan (iLok Quit, plugin
    // crash) the next launch can auto-resume without the user having to
    // click "Rescan" again. Cleared in the IncrementalScanner done branch.
    ns::FileUtils::userDataDir().createDirectory();
    ns::FileUtils::userDataDir().getChildFile ("ScanInProgress.flag")
        .replaceWithText ("1");

    auto userDone = std::move (completionCb);
    auto wrappedDone = [this, userDone = std::move (userDone)]
    {
        ns::FileUtils::userDataDir().getChildFile ("ScanInProgress.flag")
            .deleteFile();
        if (userDone) userDone();

        // Tear down the scanner asynchronously so isScanning() flips to
        // false and the overlay auto-hides. We cannot reset() from inside
        // the scanner's own timer callback.
        juce::MessageManager::callAsync ([this] { scanner.reset(); });
    };

    scanner = std::make_unique<IncrementalScanner> (*this,
                                                    std::move (progressCb),
                                                    std::move (wrappedDone));
}

bool PluginManager::shouldAutoResumeScan()
{
    return ns::FileUtils::userDataDir()
              .getChildFile ("ScanInProgress.flag")
              .existsAsFile();
}

void PluginManager::cancelAsyncScan()
{
    scanner.reset();
}

bool PluginManager::isScanning() const noexcept
{
    return scanner != nullptr;
}

void PluginManager::clearBlacklist()
{
    auto pedal = getDeadMansPedalFile();
    if (pedal.existsAsFile()) pedal.deleteFile();
}

void PluginManager::clearNeedsAuth()
{
    auto f = ns::FileUtils::userDataDir().getChildFile ("NeedsAuth.txt");
    if (f.existsAsFile()) f.deleteFile();
}

int PluginManager::getNeedsAuthCount() const
{
    auto f = ns::FileUtils::userDataDir().getChildFile ("NeedsAuth.txt");
    if (! f.existsAsFile()) return 0;
    // One fileId per non-empty line.
    juce::StringArray lines;
    lines.addLines (f.loadFileAsString());
    int n = 0;
    for (auto& l : lines) if (l.trim().isNotEmpty()) ++n;
    return n;
}

juce::String PluginManager::getScannedFormatsDescription() const
{
    juce::StringArray names;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (auto* fmt = formatManager.getFormat (i))
            names.add (fmt->getName());
    if (names.isEmpty()) return "(none)";
    return names.joinIntoString (", ");
}

std::unique_ptr<juce::AudioPluginInstance>
PluginManager::createInstance (const juce::PluginDescription& desc,
                               double sampleRate, int blockSize,
                               juce::String& errorOut)
{
    return formatManager.createPluginInstance (desc, sampleRate, blockSize, errorOut);
}

void PluginManager::beginGuardedAdd (const juce::String& fileOrIdentifier)
{
    const auto pending = ns::FileUtils::userDataDir().getChildFile ("PendingAdd.txt");
    pending.getParentDirectory().createDirectory();
    pending.replaceWithText (fileOrIdentifier);
    markScanStart (fileOrIdentifier);
}

void PluginManager::endGuardedAdd()
{
    markScanEnd();
    ns::FileUtils::userDataDir().getChildFile ("PendingAdd.txt").deleteFile();
}

bool PluginManager::validateBeforeAdd (const juce::PluginDescription& desc,
                                       double sampleRate, int blockSize,
                                       juce::String& errorOut)
{
    // Locate our own executable.
    const juce::String selfExe = ns::plat::getExecutablePath();
    if (selfExe.isEmpty())
    {
        errorOut = "could not locate host executable";
        return false;
    }

    // Write PluginDescription XML to a temp file.
    auto tmp = juce::File::createTempFile (".nsdesc.xml");
    if (auto xml = desc.createXml())
    {
        xml->writeTo (tmp, {});
    }
    else
    {
        errorOut = "could not serialize PluginDescription";
        return false;
    }

    juce::ChildProcess cp;
    juce::StringArray args { selfExe,
                             "--ns-validate-plugin",
                             tmp.getFullPathName(),
                             juce::String (sampleRate, 1),
                             juce::String (blockSize) };

    bool ok = false;
    bool timedOut = false;
    juce::String reason;

    if (! cp.start (args, juce::ChildProcess::wantStdOut))
    {
        reason = "spawn failed";
    }
    else if (! cp.waitForProcessToFinish (60000)) // up to 60 s — Neural DSP
                                                  // Archetype and other heavy
                                                  // model-loading plugins can
                                                  // legitimately need 30+ s.
    {
        cp.kill();
        timedOut = true;
        reason = "timed out (>60 s) — likely waiting on an authentication "
                 "dialog (iLok / PACE / Waves). Launch the plugin once in "
                 "another DAW to authenticate, then try Add again.";
    }
    else
    {
        const auto exitCode = cp.getExitCode();
        if (exitCode == 0)
            ok = true;
        else
            reason = "validation child exited " + juce::String (exitCode);
    }

    tmp.deleteFile();

    if (! ok)
    {
        errorOut = reason;

        if (timedOut)
        {
            // Auth/iLok timeout — don't blacklist. Park on the needs-auth
            // list so the plugin stays in the picker and the user can retry
            // after authenticating elsewhere.
            auto needs = ns::FileUtils::userDataDir().getChildFile ("NeedsAuth.txt");
            needs.getParentDirectory().createDirectory();
            auto lines = needs.existsAsFile()
                         ? juce::StringArray::fromLines (needs.loadFileAsString())
                         : juce::StringArray{};
            lines.removeEmptyStrings();
            if (! lines.contains (desc.fileOrIdentifier))
                lines.add (desc.fileOrIdentifier);
            needs.replaceWithText (lines.joinIntoString ("\n") + "\n");
        }
        else
        {
            // Permanent failure (crash, refused to load) — keep it out of
            // the picker via the dead-man's-pedal blacklist.
            const auto pedal = getDeadMansPedalFile();
            pedal.getParentDirectory().createDirectory();
            auto lines = pedal.existsAsFile()
                         ? juce::StringArray::fromLines (pedal.loadFileAsString())
                         : juce::StringArray{};
            lines.removeEmptyStrings();
            if (! lines.contains (desc.fileOrIdentifier))
                lines.add (desc.fileOrIdentifier);
            pedal.replaceWithText (lines.joinIntoString ("\n") + "\n");

            const auto types = knownPlugins.getTypes();
            for (const auto& d : types)
                if (d.fileOrIdentifier == desc.fileOrIdentifier)
                    knownPlugins.removeType (d);
            saveKnownList();
        }
    }

    return ok;
}

bool PluginManager::handleCrashedAddOnLaunch()
{
    const auto pending = ns::FileUtils::userDataDir().getChildFile ("PendingAdd.txt");
    if (! pending.existsAsFile()) return false;
    const auto fileId = pending.loadFileAsString().trim();
    pending.deleteFile();
    if (fileId.isEmpty()) return false;

    // Add to blacklist.
    const auto pedal = getDeadMansPedalFile();
    pedal.getParentDirectory().createDirectory();
    auto lines = pedal.existsAsFile()
                 ? juce::StringArray::fromLines (pedal.loadFileAsString())
                 : juce::StringArray{};
    lines.removeEmptyStrings();
    if (! lines.contains (fileId))
        lines.add (fileId);
    pedal.replaceWithText (lines.joinIntoString ("\n") + "\n");

    // Remove every type that points at this fileOrIdentifier from the
    // known-list so the picker never offers it again.
    auto types = knownPlugins.getTypes();
    for (const auto& d : types)
        if (d.fileOrIdentifier == fileId)
            knownPlugins.removeType (d);
    saveKnownList();
    return true;
}
