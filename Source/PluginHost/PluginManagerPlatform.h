#pragma once

#include <juce_core/juce_core.h>

/**
    Cross-platform helpers used by PluginManager for the guardian-subprocess
    crash-protection subsystem.

    The original macOS implementation uses kqueue NOTE_EXIT + posix_spawn +
    _NSGetExecutablePath. The same pattern is implemented natively on
    Windows (WaitForSingleObject on the parent process handle + CreateProcessW)
    and Linux (polling kill(pid,0) + fork/execvp).

    Semantics summary:
      1. App startup calls launchGuardianChild() once. That re-execs ourselves
         with `--ns-guardian <ourPid> <appLaunchTarget>` as a detached child.
      2. main() detects --ns-guardian and routes into the guardian flow:
           waitForProcessExit(parentPid);   // blocks until parent dies
           if (marker file exists)
               relaunchApp(appLaunchTarget);
      3. Markers are written before each plugin scan/load by PluginManager
         and removed after; so the guardian only re-launches us when we die
         INSIDE a plugin load (iLok SIGKILL, hard crash, abort()...).
*/
namespace ns::plat
{
    /** Absolute path to the currently-running executable. */
    juce::String getExecutablePath();

    /** Best path to hand to the OS for "launch this app again".
        On macOS this walks up from the executable until it finds the .app
        bundle and returns that. On Windows / Linux there is no bundle
        concept, so it returns the executable path itself. */
    juce::String getAppLaunchTarget();

    /** Current process id (portable; fits in an int on all supported OSes). */
    int getProcessId();

    /** Spawn a detached copy of `selfExe` with the argv:
            <selfExe> --ns-guardian <ourPid> <appLaunchTarget>
        The new process is detached so it survives our death.
        Returns the child pid on success, or 0 on failure. */
    int launchGuardianChild (const juce::String& selfExe,
                             int                ourPid,
                             const juce::String& appLaunchTarget);

    /** Block until the process identified by parentPid exits.
        Returns immediately if the process is already gone. */
    void waitForProcessExit (int parentPid);

    /** Suspend the calling thread for the given number of milliseconds. */
    void sleepMs (int ms);

    /** Best-effort detached re-launch of the app at the given target path.
        On macOS uses `/usr/bin/open -n <bundle>`; on Windows uses
        CreateProcessW(DETACHED_PROCESS); on Linux uses fork + execvp. */
    void relaunchApp (const juce::String& appLaunchTarget);

    /** Exit the current process immediately, skipping atexit / static
        destructors. Used by the scan/validate child processes. */
    [[noreturn]] void exitImmediate (int code);
}
