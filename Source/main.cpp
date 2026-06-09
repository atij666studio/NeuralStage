#include "App.h"

// In plugin builds (VST3 / CLAP) JUCE generates its own entry point and the
// createPluginFilter() factory in NeuralStageProcessor.cpp is used instead.
// This whole file must be excluded from plugin compilation.
#ifndef NS_BUILD_PLUGIN

extern int neuralstage_maybe_run_guardian       (int, const char* const*);
extern int neuralstage_maybe_run_plugin_scan    (int, const char* const*);
extern int neuralstage_maybe_run_plugin_validate (int, const char* const*);

static juce::JUCEApplicationBase* createNeuralStageApp() { return new App(); }

static int neuralstage_run (int argc, const char* const* argv)
{
    if (neuralstage_maybe_run_guardian (argc, argv))
        return 0;
    // Plugin-scan + validate child modes never return (they call _exit).
    // If they return 0 we are not the child — fall through to normal app boot.
    neuralstage_maybe_run_plugin_scan     (argc, argv);
    neuralstage_maybe_run_plugin_validate (argc, argv);
    juce::JUCEApplicationBase::createInstance = &createNeuralStageApp;
   #if JUCE_WINDOWS
    // On Windows GUI subsystem JUCE only defines the no-arg overload; it
    // recovers argv from GetCommandLineW() internally.
    return juce::JUCEApplicationBase::main();
   #else
    return juce::JUCEApplicationBase::main (argc, const_cast<const char**> (argv));
   #endif
}

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shellapi.h>

 int APIENTRY WinMain (HINSTANCE, HINSTANCE, LPSTR, int)
 {
     // __argc / __argv are provided by the MSVC CRT and reflect the parsed
     // command line — adequate for our guardian/scan-child argv checks.
     return neuralstage_run (__argc, const_cast<const char* const*> (__argv));
 }
#else
 int main (int argc, char* argv[])
 {
     return neuralstage_run (argc, const_cast<const char* const*> (argv));
 }
#endif

#endif // NS_BUILD_PLUGIN
