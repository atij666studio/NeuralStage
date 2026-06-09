// NamForceLink.cpp
//
// Ensures every NAM architecture parser is registered in ConfigParserRegistry
// before the first get_dsp() call. This is the primary NAM registration path
// for the VST3/CLAP plugin DLL.
//
// Root cause of why file-scope statics (ConfigParserHelper in wavenet/model.cpp,
// lstm.cpp, convnet.cpp, dsp.cpp, container.cpp) don't fire in the plugin DLL:
//
//   MSVC's linker dead-code-strips entire object files from a static library
//   when no exported/referenced symbol in those files is used by the DLL.
//   The file-scope ConfigParserHelper statics are the ONLY thing in those
//   object files, and they register into a singleton that the linker cannot
//   see as "used" from outside. Result: all five architecture TUs are stripped,
//   their statics never fire, ConfigParserRegistry is empty when get_dsp() runs,
//   and every NAM load fails with "No config parser registered for architecture".
//
// This fix:
//   1. Takes the address of a symbol from EACH architecture TU so the linker
//      is forced to include each object file in the DLL link.
//   2. Also registers each architecture directly (belt-and-suspenders in case
//      LTCG inlines and eliminates the static array trick).
//   This function is called from NAMProcessor::NAMProcessor() and from
//   NAMProcessor::loadSlot(), guaranteeing it runs before any get_dsp() call.

#if NS_HAVE_NAM_CORE
 #include "NAM/container.h"
 #include "NAM/convnet.h"
 #include "NAM/lstm.h"
 #include "NAM/dsp.h"
 #include "NAM/wavenet/model.h"
 #include "NAM/model_config.h"
 #ifdef _MSC_VER
  #pragma comment(linker, "/WHOLEARCHIVE:nam_core.lib")
 #endif
#endif

#include "NamForceLink.h"

void ns_ensureNamContainerRegistered() noexcept
{
#if NS_HAVE_NAM_CORE
    // ── Step 1: Force all five architecture TUs into the link ────────────────
    // Taking the address of a function from each TU creates a cross-TU
    // reference that the linker cannot remove. The volatile array prevents
    // the compiler from treating these as dead stores.
    static volatile void* const kForceLinks[] = {
        reinterpret_cast<void*> (&nam::container::create_config),
        reinterpret_cast<void*> (&nam::convnet::create_config),
        reinterpret_cast<void*> (&nam::lstm::create_config),
        reinterpret_cast<void*> (&nam::linear::create_config),
        reinterpret_cast<void*> (&nam::wavenet::create_config),
    };
    (void) kForceLinks;

    // ── Step 2: Register directly (LTCG / cross-module belt-and-suspenders) ──
    // With MSVC LTCG the linker may inline step 1's static-init entirely.
    // Explicit registration with a has() guard is always correct regardless of
    // initialization order: whichever path fires first wins; the other is a no-op.
    auto& reg = nam::ConfigParserRegistry::instance();

    auto tryReg = [&] (const char* name, nam::ConfigParserFunction fn) noexcept
    {
        try {
            if (! reg.has (name))
                reg.registerParser (name, std::move (fn));
        }
        catch (...) {}
    };

    tryReg ("WaveNet",            [] (const nlohmann::json& c, double sr) -> std::unique_ptr<nam::ModelConfig>
                                  { return nam::wavenet::create_config (c, sr); });
    tryReg ("LSTM",               [] (const nlohmann::json& c, double sr) -> std::unique_ptr<nam::ModelConfig>
                                  { return nam::lstm::create_config (c, sr); });
    tryReg ("ConvNet",            [] (const nlohmann::json& c, double sr) -> std::unique_ptr<nam::ModelConfig>
                                  { return nam::convnet::create_config (c, sr); });
    tryReg ("Linear",             [] (const nlohmann::json& c, double sr) -> std::unique_ptr<nam::ModelConfig>
                                  { return nam::linear::create_config (c, sr); });
    tryReg ("SlimmableContainer", [] (const nlohmann::json& c, double sr) -> std::unique_ptr<nam::ModelConfig>
                                  { return nam::container::create_config (c, sr); });
#endif
}
