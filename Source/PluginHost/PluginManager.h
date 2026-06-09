#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

/** Manages plugin formats, scanning, and the cached KnownPluginList.
 *  Persists list to ~/Library/Application Support/NeuralStage/KnownPlugins.xml.
 */
class PluginManager
{
public:
    PluginManager();
    ~PluginManager();

    void initialiseFormats();
    void loadKnownList();
    void saveKnownList() const;

    /** Synchronous scan of a single format (VST3 or AU). UI thread OK. */
    void scanFormat (juce::AudioPluginFormat& format,
                     const juce::FileSearchPath& searchPaths,
                     std::function<void (const juce::String&)> progressCb = {});

    /** Convenience: scan all installed formats with default search paths. */
    void scanAllInstalled (std::function<void (const juce::String&)> progressCb = {});

    /** Begin an incremental scan that runs on the message thread (one plugin per tick).
     *  Safer than background-thread scanning: AU/VST3 loaders can touch AppKit.
     *  Calls progressCb on each step, completionCb when done. Cancels any in-flight scan.
     */
    void beginAsyncScan (std::function<void (const juce::String&)> progressCb,
                         std::function<void ()> completionCb);
    void cancelAsyncScan();
    bool isScanning() const noexcept;

    /** True iff a previous scan was interrupted (process died) and we
     *  should auto-resume on next launch.
     */
    static bool shouldAutoResumeScan();

    /** Latest plugin file/path being scanned (thread-safe snapshot). */
    juce::String getCurrentScanText() const;
    /** Approximate progress 0..1 across all formats during an async scan. */
    float        getScanProgress() const noexcept { return scanProgress.load(); }

    /** Wipes the dead-man's-pedal blacklist so previously-failed plugins
     *  will be retried on the next scan.
     */
    void clearBlacklist();

    /** Wipes the needs-authentication list (iLok / PACE / Waves plugins
     *  that timed out during a previous scan because of an auth dialog).
     *  Useful as a "force rescan" after the user has authenticated.
     */
    void clearNeedsAuth();

    /** Number of plugins currently parked on the needs-auth list (i.e.
     *  timed out during their last scan because an auth dialog was up).
     *  Cheap O(N) read from disk; safe to call from the UI thread once
     *  per second or so. */
    int  getNeedsAuthCount() const;

    /** Human-readable summary of which plugin formats this build will scan
     *  on this platform (e.g. "VST3, AU, LV2" on macOS, "VST3, LV2" on
     *  Windows). Computed from the registered format manager so it stays
     *  in sync with whatever JUCE_PLUGINHOST_* defines were enabled at
     *  compile time. */
    juce::String getScannedFormatsDescription() const;

    /** Called once at construction (and exposed for tests). If a plugin
     *  instantiation killed the host on the previous launch, the
     *  PendingAdd marker is still on disk — blacklist that fileId and
     *  remove its types from the known-list. Returns true if a crashed
     *  add was recovered.
     */
    bool handleCrashedAddOnLaunch();

    /** Arms the kqueue guardian and writes a PendingAdd marker for the
     *  given plugin file/identifier. The marker is consumed by
     *  handleCrashedAddOnLaunch() on the next launch if we die before
     *  endGuardedAdd() is called. Use the RAII helper GuardedAdd below
     *  to wrap the entire add+prepare flow. */
    void beginGuardedAdd (const juce::String& fileOrIdentifier);
    void endGuardedAdd();

    struct GuardedAdd
    {
        GuardedAdd (PluginManager& m, const juce::String& fileId) : owner (m)
        {
            owner.beginGuardedAdd (fileId);
        }
        ~GuardedAdd() { owner.endGuardedAdd(); }
        PluginManager& owner;
    };

    juce::AudioPluginFormatManager& getFormats()      noexcept { return formatManager; }
    juce::KnownPluginList&          getKnownList()    noexcept { return knownPlugins; }

    std::unique_ptr<juce::AudioPluginInstance>
        createInstance (const juce::PluginDescription& desc,
                        double sampleRate, int blockSize,
                        juce::String& errorOut);

    /** Pre-flights a plugin by spawning a child process that creates the
     *  instance and calls prepareToPlay. If the child crashes / hangs /
     *  exits non-zero, the plugin is added to the dead-mans-pedal and
     *  removed from the known-list. Returns true if the plugin is safe
     *  to load in this process. Blocks for up to ~30 s. UI thread.
     */
    bool validateBeforeAdd (const juce::PluginDescription& desc,
                            double sampleRate, int blockSize,
                            juce::String& errorOut);

    static juce::File getKnownListFile();
    static juce::File getDeadMansPedalFile();

private:
    void setCurrentScanText (const juce::String&);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList          knownPlugins;

    class IncrementalScanner;
    std::unique_ptr<IncrementalScanner> scanner;

    mutable juce::CriticalSection scanTextLock;
    juce::String                  currentScanText;
    std::atomic<float>            scanProgress { 0.0f };
};
