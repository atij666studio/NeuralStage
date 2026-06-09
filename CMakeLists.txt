cmake_minimum_required(VERSION 3.22)

# --- Force native arm64 on Apple Silicon (avoid x86_64 anaconda toolchain) ---
if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND /usr/sbin/sysctl -in hw.optional.arm64
                    OUTPUT_VARIABLE _is_arm64 OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (_is_arm64 STREQUAL "1")
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(NeuralStage VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Windows: ASIO SDK (shared from AtiNAMatiC-Tone, mirrors AtiNAMatiC-Random setup) ---
if (MSVC)
    set(JUCE_ASIOSDK_PATH "G:/MY PLUGINS/AtiNAMatiC Series/AtiNAMatiC-Tone/juce-vst3-bridge/ASIOSDK")
endif()

# --- Pull JUCE via FetchContent if not vendored ---
if (EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/JUCE/CMakeLists.txt")
    add_subdirectory(ThirdParty/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.8
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# --- clap-juce-extensions (publishes the plugin target in CLAP format) ---
include(FetchContent)
FetchContent_Declare(
    clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(clap_juce_extensions)

# --- NAM core (vendored at ThirdParty/NeuralAmpModelerCore) ---
set(NAM_CORE_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/NeuralAmpModelerCore")
if (EXISTS "${NAM_CORE_DIR}/NAM/dsp.h")
    file(GLOB NAM_CORE_SOURCES CONFIGURE_DEPENDS
        "${NAM_CORE_DIR}/NAM/*.cpp"
        "${NAM_CORE_DIR}/NAM/wavenet/*.cpp"
    )
    add_library(nam_core STATIC ${NAM_CORE_SOURCES})
    set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(nam_core PUBLIC cxx_std_20)
    target_include_directories(nam_core SYSTEM PUBLIC
        "${NAM_CORE_DIR}"
        "${NAM_CORE_DIR}/NAM"
        "${NAM_CORE_DIR}/Dependencies/eigen"
        "${NAM_CORE_DIR}/Dependencies/nlohmann"
        "${NAM_CORE_DIR}/Dependencies/AudioDSPTools"
    )
    target_compile_definitions(nam_core PUBLIC NAM_HAVE_CORE=1 NAM_SAMPLE_FLOAT=1)
    if (APPLE)
        target_compile_options(nam_core PUBLIC -stdlib=libc++)
    endif()
    set(NS_HAVE_NAM_CORE TRUE)
else()
    message(STATUS "NAM core not vendored — building NAMProcessor as passthrough.")
    set(NS_HAVE_NAM_CORE FALSE)
endif()

juce_add_gui_app(NeuralStage
    PRODUCT_NAME              "NeuralStage"
    COMPANY_NAME              "Atij666Studio"
    BUNDLE_ID                 "com.neuralstage.app"
    ICON_BIG                  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NeuralStage needs microphone/input access for guitar input."
)

juce_generate_juce_header(NeuralStage)

# --- Binary resources (splash image, etc.) ---
juce_add_binary_data(NeuralStageBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES
        Resources/ls.png
)
set_target_properties(NeuralStageBinaryData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(NeuralStage PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NeuralStage,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NeuralStage,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)

# --- Windows audio backends (ASIO + WASAPI + DirectSound) ---
if (MSVC)
    target_compile_definitions(NeuralStage PRIVATE
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_WASAPI_EXCLUSIVE=1
        JUCE_DIRECTSOUND=1)
    target_include_directories(NeuralStage PRIVATE
        "${JUCE_ASIOSDK_PATH}/common"
        "${JUCE_ASIOSDK_PATH}/host"
        "${JUCE_ASIOSDK_PATH}/host/pc")
    # Force MSVC to read source files as UTF-8 (and emit UTF-8 execution
    # strings). Without this flag, any string literal containing non-ASCII
    # characters -- em-dashes, smart quotes, etc. -- gets interpreted under
    # the active system codepage (CP1252), producing mojibake like "aEUR""
    # in the running UI.
    target_compile_options(NeuralStage PRIVATE /utf-8)
endif()

# --- Linux audio backends (ALSA + JACK + PipeWire; JUCE handles linkage via pkg-config) ---
if (UNIX AND NOT APPLE)
    target_compile_definitions(NeuralStage PRIVATE
        JUCE_ALSA=1
        JUCE_JACK=1
        JUCE_USE_PIPEWIRE=1
        WDL_ALLOW_UNSIGNED_DEFAULT_CHAR=1)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(NeuralStage PRIVATE -msse4.1)
    endif()
endif()

# --- Source tree ---
file(GLOB_RECURSE NS_SOURCES CONFIGURE_DEPENDS
    Source/*.cpp
    Source/*.mm
    Source/*.h
)

# Objective-C++ (.mm) files are macOS-only — strip them from non-Apple builds
# so MSVC / GCC / Clang on Windows + Linux don't try to compile them.
if (NOT APPLE)
    list(FILTER NS_SOURCES EXCLUDE REGEX "\\.mm$")
endif()

# Source/Plugin/ contains the AudioProcessor wrapper (NeuralStageProcessor /
# NeuralStageEditor).  The standalone app does NOT need those files, so we
# partition the source list.
# NOTE: use /Plugin/ (with trailing slash) so we don't accidentally exclude
#       Source/PluginHost/ which is needed by both targets.
set(NS_SOURCES_COMMON ${NS_SOURCES})
list(FILTER NS_SOURCES_COMMON EXCLUDE REGEX "/Plugin/")

set(NS_SOURCES_PLUGIN ${NS_SOURCES})
list(FILTER NS_SOURCES_PLUGIN INCLUDE REGEX "/Plugin/")

target_sources(NeuralStage PRIVATE ${NS_SOURCES_COMMON})

target_include_directories(NeuralStage PRIVATE
    Source
    Source/UI
    Source/UI/Panels
    Source/UI/Components
    Source/UI/Bars
    Source/UI/Styles
    Source/Audio
    Source/Audio/Processors
    Source/Audio/NAM
    Source/Audio/IR
    Source/Audio/FX
    Source/Audio/Tuner
    Source/PluginHost
    Source/MIDI
    Source/Core
    Source/Utils
)

# Pretty up the IDE tree
source_group(TREE ${CMAKE_SOURCE_DIR}/Source PREFIX "Source" FILES ${NS_SOURCES})

target_link_libraries(NeuralStage PRIVATE
    NeuralStageBinaryData
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if (NS_HAVE_NAM_CORE)
    target_link_libraries(NeuralStage PRIVATE nam_core)
    target_compile_definitions(NeuralStage PRIVATE NS_HAVE_NAM_CORE=1)
    # The vendored AudioDSPTools ResamplingContainer (used by ResamplingNAM)
    # was lifted from iPlug2 and pulls in WDL headers that include <windows.h>.
    # NOMINMAX stops windows.h defining the min()/max() function-like macros
    # that otherwise break std::min/std::max inside LanczosResampler.h, and
    # DEFAULT_BLOCK_SIZE supplies the iPlug2 constant the resampler expects.
    target_compile_definitions(NeuralStage PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN DEFAULT_BLOCK_SIZE=1024)
    # Force-load so static initializers (architecture parsers like WaveNet/LSTM)
    # in nam_core aren't stripped by the linker.
    if (APPLE)
        target_link_options(NeuralStage PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:nam_core>")
    elseif (UNIX)
        target_link_options(NeuralStage PRIVATE
            "LINKER:--whole-archive" "$<TARGET_FILE:nam_core>" "LINKER:--no-whole-archive")
    elseif (MSVC)
        # Bare name: link.exe resolves this via LIB search paths at build time.
        # The explicit ns_ensureNamContainerRegistered() calls in NAMProcessor
        # constructor/loadSlot are the primary registration guarantee on MSVC.
        target_link_options(NeuralStage PRIVATE "/WHOLEARCHIVE:nam_core.lib")
    endif()
endif()

# ==============================================================================
# NeuralStagePlugin — VST3 + CLAP plugin target
# ==============================================================================

juce_add_plugin(NeuralStagePlugin
    PRODUCT_NAME              "NeuralStage"
    COMPANY_NAME              "Atij666Studio"
    BUNDLE_ID                 "com.neuralstage.plugin"
    PLUGIN_MANUFACTURER_CODE  Nstg
    PLUGIN_CODE               NST1
    FORMATS                   VST3 CLAP
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           "Fx"
    CLAP_ID                   "com.neuralstage.plugin"
    CLAP_FEATURES             audio-effect
    ICON_BIG                  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NeuralStage needs input access for guitar."
)

juce_generate_juce_header(NeuralStagePlugin)

target_compile_definitions(NeuralStagePlugin PRIVATE
    NS_BUILD_PLUGIN=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="NeuralStage"
    JUCE_APPLICATION_VERSION_STRING="0.2.1"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    # NeuralStage has never had a VST2 release — suppress the JUCE 8.0.6+ parameter-ID
    # mismatch warning that fires when JUCE_VST3_CAN_REPLACE_VST2 is left at its default.
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)

if (MSVC)
    target_compile_definitions(NeuralStagePlugin PRIVATE
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_WASAPI_EXCLUSIVE=1
        JUCE_DIRECTSOUND=1)
    target_include_directories(NeuralStagePlugin PRIVATE
        "${JUCE_ASIOSDK_PATH}/common"
        "${JUCE_ASIOSDK_PATH}/host"
        "${JUCE_ASIOSDK_PATH}/host/pc")
    target_compile_options(NeuralStagePlugin PRIVATE /utf-8)
endif()

if (UNIX AND NOT APPLE)
    target_compile_definitions(NeuralStagePlugin PRIVATE
        JUCE_ALSA=1
        JUCE_JACK=1
        JUCE_USE_PIPEWIRE=1
        WDL_ALLOW_UNSIGNED_DEFAULT_CHAR=1)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(NeuralStagePlugin PRIVATE -msse4.1)
    endif()
endif()

target_sources(NeuralStagePlugin PRIVATE
    ${NS_SOURCES_COMMON}
    ${NS_SOURCES_PLUGIN}
)

target_include_directories(NeuralStagePlugin PRIVATE
    Source
    Source/UI
    Source/UI/Panels
    Source/UI/Components
    Source/UI/Bars
    Source/UI/Styles
    Source/Audio
    Source/Audio/Processors
    Source/Audio/NAM
    Source/Audio/IR
    Source/Audio/FX
    Source/Audio/Tuner
    Source/PluginHost
    Source/MIDI
    Source/Core
    Source/Utils
    Source/Plugin
)

# IDE folder organisation for plugin sources
source_group(TREE ${CMAKE_SOURCE_DIR}/Source PREFIX "Source" FILES ${NS_SOURCES_COMMON} ${NS_SOURCES_PLUGIN})

target_link_libraries(NeuralStagePlugin PRIVATE
    NeuralStageBinaryData
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if (NS_HAVE_NAM_CORE)
    target_link_libraries(NeuralStagePlugin PRIVATE nam_core)
    target_compile_definitions(NeuralStagePlugin PRIVATE NS_HAVE_NAM_CORE=1 NOMINMAX WIN32_LEAN_AND_MEAN DEFAULT_BLOCK_SIZE=1024)
    if (APPLE)
        target_link_options(NeuralStagePlugin PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:nam_core>")
    elseif (UNIX)
        target_link_options(NeuralStagePlugin PRIVATE
            "LINKER:--whole-archive" "$<TARGET_FILE:nam_core>" "LINKER:--no-whole-archive")
    elseif (MSVC)
        target_link_options(NeuralStagePlugin PRIVATE "/WHOLEARCHIVE:nam_core.lib")
    endif()
endif()

# Add CLAP format support via clap-juce-extensions.
# This must come AFTER target_link_libraries.
clap_juce_extensions_plugin(
    TARGET NeuralStagePlugin
    CLAP_ID "com.neuralstage.plugin"
    CLAP_FEATURES audio-effect
)

# ==============================================================================
# Versioned output file names  (NeuralStage.v.X.Y.Z.exe / .clap)
# The version is taken from project(... VERSION ...) at the top of this file.
#
# NOTE: VST3 bundles deliberately keep the plain name "NeuralStage.vst3".
# DAWs (Reaper, Cubase, Live, etc.) identify plugins by bundle name — if we
# rename the bundle each release, every saved project loses the plugin.  The
# version is still embedded inside the bundle's moduleinfo.json.
# ==============================================================================
set(_NS_VER "${PROJECT_VERSION}")

# Standalone exe  →  NeuralStage.v.X.Y.Z.exe
set_target_properties(NeuralStage PROPERTIES OUTPUT_NAME "NeuralStage.v.${_NS_VER}")

# CLAP file  →  NeuralStage.v.X.Y.Z.clap  (CLAP plugins are identified by
# CLAP_ID, not filename, so a versioned filename is safe here)
if (TARGET NeuralStagePlugin_CLAP)
    set_target_properties(NeuralStagePlugin_CLAP PROPERTIES
        OUTPUT_NAME "NeuralStage.v.${_NS_VER}")
endif()
