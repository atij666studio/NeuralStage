#include "PluginManagerPlatform.h"

#include <chrono>
#include <cstdlib>
#include <thread>

#if JUCE_MAC
 #include <fcntl.h>
 #include <signal.h>
 #include <spawn.h>
 #include <sys/event.h>
 #include <unistd.h>
 #include <mach-o/dyld.h>
 extern char** environ;
#elif JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <process.h>
#else // JUCE_LINUX / other POSIX
 #include <errno.h>
 #include <signal.h>
 #include <spawn.h>
 #include <sys/types.h>
 #include <sys/wait.h>
 #include <unistd.h>
 extern char** environ;
#endif

namespace ns::plat
{

// ----- getExecutablePath ----------------------------------------------------
juce::String getExecutablePath()
{
#if JUCE_MAC
    char buf[4096]; uint32_t sz = sizeof (buf);
    if (_NSGetExecutablePath (buf, &sz) != 0) return {};
    return juce::String (buf);
#elif JUCE_WINDOWS
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW (nullptr, buf, (DWORD) (sizeof (buf) / sizeof (wchar_t)));
    if (n == 0) return {};
    return juce::String (juce::CharPointer_UTF16 ((const juce::CharPointer_UTF16::CharType*) buf));
#else
    char buf[4096];
    ssize_t n = ::readlink ("/proc/self/exe", buf, sizeof (buf) - 1);
    if (n <= 0) return {};
    buf[n] = 0;
    return juce::String (buf);
#endif
}

// ----- getAppLaunchTarget ---------------------------------------------------
juce::String getAppLaunchTarget()
{
    auto exe = getExecutablePath();
    if (exe.isEmpty()) return {};

#if JUCE_MAC
    // Walk up to the enclosing .app bundle.
    auto p = juce::File (exe);
    while (p.exists() && ! p.getFileName().endsWith (".app")
           && p.getParentDirectory() != p)
        p = p.getParentDirectory();
    return p.getFileName().endsWith (".app") ? p.getFullPathName() : exe;
#else
    // No bundle concept on Windows/Linux.
    return exe;
#endif
}

// ----- getProcessId ---------------------------------------------------------
int getProcessId()
{
#if JUCE_WINDOWS
    return (int) ::GetCurrentProcessId();
#else
    return (int) ::getpid();
#endif
}

// ----- sleepMs --------------------------------------------------------------
void sleepMs (int ms)
{
    if (ms <= 0) return;
    std::this_thread::sleep_for (std::chrono::milliseconds (ms));
}

// ----- exitImmediate --------------------------------------------------------
[[noreturn]] void exitImmediate (int code)
{
#if JUCE_WINDOWS
    ::ExitProcess ((UINT) code);
#else
    ::_exit (code);
#endif
}

// ----- launchGuardianChild --------------------------------------------------
int launchGuardianChild (const juce::String& selfExe,
                         int                 ourPid,
                         const juce::String& appLaunchTarget)
{
    if (selfExe.isEmpty()) return 0;

#if JUCE_MAC
    const std::string sExe = selfExe.toStdString();
    const std::string sPid = std::to_string (ourPid);
    const std::string sTgt = appLaunchTarget.toStdString();
    const char* a[] = { sExe.c_str(), "--ns-guardian", sPid.c_str(), sTgt.c_str(), nullptr };

    pid_t pid = 0;
    posix_spawnattr_t attr;
    posix_spawnattr_init (&attr);
    posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETSIGDEF);
    int rc = posix_spawn (&pid, sExe.c_str(), nullptr, &attr,
                          const_cast<char* const*> (a), environ);
    posix_spawnattr_destroy (&attr);
    return (rc == 0) ? (int) pid : 0;

#elif JUCE_WINDOWS
    // Build quoted command line: "<selfExe>" --ns-guardian <pid> "<target>"
    juce::String cmd;
    cmd << "\"" << selfExe << "\" --ns-guardian "
        << juce::String (ourPid) << " \""
        << appLaunchTarget << "\"";

    std::wstring wCmd (cmd.toWideCharPointer());
    std::vector<wchar_t> mutableCmd (wCmd.begin(), wCmd.end());
    mutableCmd.push_back (L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof (si);
    PROCESS_INFORMATION pi{};

    DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW;

    BOOL ok = ::CreateProcessW (nullptr,
                                mutableCmd.data(),
                                nullptr, nullptr,
                                FALSE,
                                flags,
                                nullptr,
                                nullptr,
                                &si, &pi);
    if (! ok) return 0;
    int childPid = (int) pi.dwProcessId;
    ::CloseHandle (pi.hThread);
    ::CloseHandle (pi.hProcess); // detach
    return childPid;

#else // Linux
    pid_t pid = ::fork();
    if (pid < 0) return 0;
    if (pid == 0)
    {
        // Child: detach from controlling terminal then exec.
        ::setsid();
        const std::string sExe = selfExe.toStdString();
        const std::string sPid = std::to_string (ourPid);
        const std::string sTgt = appLaunchTarget.toStdString();
        const char* a[] = { sExe.c_str(), "--ns-guardian", sPid.c_str(), sTgt.c_str(), nullptr };
        ::execvp (sExe.c_str(), const_cast<char* const*> (a));
        ::_exit (127);
    }
    return (int) pid;
#endif
}

// ----- waitForProcessExit ---------------------------------------------------
void waitForProcessExit (int parentPid)
{
    if (parentPid <= 0) return;

#if JUCE_MAC
    int kq = ::kqueue();
    if (kq < 0) return;
    struct kevent ev;
    EV_SET (&ev, parentPid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (::kevent (kq, &ev, 1, nullptr, 0, nullptr) >= 0)
    {
        struct kevent triggered;
        ::kevent (kq, nullptr, 0, &triggered, 1, nullptr); // blocks until exit
    }
    ::close (kq);

#elif JUCE_WINDOWS
    HANDLE h = ::OpenProcess (SYNCHRONIZE, FALSE, (DWORD) parentPid);
    if (h == nullptr) return; // already gone or denied
    ::WaitForSingleObject (h, INFINITE);
    ::CloseHandle (h);

#else // Linux
    // Poll kill(pid, 0): returns 0 while pid is alive (signal 0 is "just check").
    // ESRCH means the process is gone.
    while (true)
    {
        if (::kill ((pid_t) parentPid, 0) == -1 && errno == ESRCH)
            return;
        sleepMs (100);
    }
#endif
}

// ----- relaunchApp ----------------------------------------------------------
void relaunchApp (const juce::String& appLaunchTarget)
{
    if (appLaunchTarget.isEmpty()) return;

#if JUCE_MAC
    // Use /usr/bin/open -n so we get a fresh instance.
    const std::string b = appLaunchTarget.toStdString();
    const char* a[] = { "open", "-n", b.c_str(), nullptr };
    pid_t child = 0;
    ::posix_spawn (&child, "/usr/bin/open", nullptr, nullptr,
                   const_cast<char* const*> (a), environ);

#elif JUCE_WINDOWS
    juce::String cmd;
    cmd << "\"" << appLaunchTarget << "\"";
    std::wstring wCmd (cmd.toWideCharPointer());
    std::vector<wchar_t> mutableCmd (wCmd.begin(), wCmd.end());
    mutableCmd.push_back (L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof (si);
    PROCESS_INFORMATION pi{};
    BOOL ok = ::CreateProcessW (nullptr, mutableCmd.data(),
                                nullptr, nullptr, FALSE,
                                DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                                nullptr, nullptr, &si, &pi);
    if (ok)
    {
        ::CloseHandle (pi.hThread);
        ::CloseHandle (pi.hProcess);
    }

#else // Linux
    pid_t pid = ::fork();
    if (pid == 0)
    {
        ::setsid();
        const std::string b = appLaunchTarget.toStdString();
        const char* a[] = { b.c_str(), nullptr };
        ::execvp (b.c_str(), const_cast<char* const*> (a));
        ::_exit (127);
    }
#endif
}

} // namespace ns::plat
