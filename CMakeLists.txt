cmake_minimum_required(VERSION 3.24)

project(AlptugDUB VERSION 1.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS 11 and newer")

# Dependencies, pinned: a local copy in deps/ is used when present, otherwise they are downloaded.
include(FetchContent)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE/CMakeLists.txt")
    add_subdirectory(deps/JUCE)
else()
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 3fb5cbc0340758596dac39f7c8de47d562b4896e) # 9.0.4
    FetchContent_MakeAvailable(JUCE)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/signalsmith-stretch/CMakeLists.txt")
    add_subdirectory(deps/signalsmith-stretch)
else()
    FetchContent_Declare(signalsmith-stretch GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
        GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df) # 1.4.0
    FetchContent_MakeAvailable(signalsmith-stretch)
endif()

juce_add_plugin(AlptugDUB
    COMPANY_NAME "Alptuğ Dağ"
    COMPANY_WEBSITE "https://github.com/AlptugDag/AlptugDUB"
    BUNDLE_ID "com.alptugdag.alptugdub"
    PLUGIN_MANUFACTURER_CODE Alpt
    PLUGIN_CODE Adub
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "AlptugDUB"
    DESCRIPTION "Natural voice characters for voice-over and dubbing, by Alptuğ Dağ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Pitch Shift
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_SANDBOX_SAFE TRUE
    HARDENED_RUNTIME_ENABLED TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(AlptugDUBAssets SOURCES
    assets/signature.svg
    assets/creator.jpg
    assets/wood.jpg
    assets/Poppins-Black.ttf
    assets/Poppins-Bold.ttf
    assets/Poppins-Medium.ttf)

target_sources(AlptugDUB PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/Look.cpp)

target_compile_definitions(AlptugDUB PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AlptugDUB
    PRIVATE
        AlptugDUBAssets
        signalsmith-stretch
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Renders the editor to a PNG (for the README and for checking the design without a host).
juce_add_console_app(AlptugDUBSnapshot PRODUCT_NAME "AlptugDUBSnapshot")
target_sources(AlptugDUBSnapshot PRIVATE tools/snapshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/ui/Look.cpp)
target_compile_definitions(AlptugDUBSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(AlptugDUBSnapshot PRIVATE AlptugDUBAssets signalsmith-stretch juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)

# The command-line test bench (no JUCE): measures pitch, formants, clicks and level.
add_executable(dubtest tools/dubtest.cpp)
target_compile_options(dubtest PRIVATE -O2)
