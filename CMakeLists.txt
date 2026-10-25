cmake_minimum_required(VERSION 3.22)
project(SolIntent VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE: use a local copy if present, otherwise download it ----
set(JUCE_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Optional local JUCE checkout")
if(EXISTS "${JUCE_LOCAL_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

option(SOL_BUILD_TESTS "Build the offline test" OFF)

set(SOL_SOURCES
    Source/Library.cpp
    Source/SamplerVoice.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/MapView.cpp
    Source/EditorMain.cpp
    Source/Panels.cpp
    Source/roll/RollEngine.cpp
    Source/gui/RollPage.cpp)

set(SOL_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(SOL_LIBS
    juce::juce_audio_utils
    juce::juce_audio_formats)

# Change PRODUCT_NAME here to rename the plugin everywhere.
juce_add_plugin(SolIntent
    COMPANY_NAME "Sol Intent"
    PLUGIN_MANUFACTURER_CODE SolI
    PLUGIN_CODE SolB
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Sol Intent"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SolIntent PRIVATE ${SOL_SOURCES})
target_compile_definitions(SolIntent PUBLIC ${SOL_DEFS})
target_link_libraries(SolIntent
    PRIVATE ${SOL_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(SOL_BUILD_TESTS)
    juce_add_console_app(SolTest PRODUCT_NAME "SolTest")
    target_sources(SolTest PRIVATE ${SOL_SOURCES} tools/OfflineTest.cpp)
    target_compile_definitions(SolTest PRIVATE ${SOL_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Sol Intent")
    target_link_libraries(SolTest PRIVATE ${SOL_LIBS} juce::juce_recommended_config_flags)
endif()
