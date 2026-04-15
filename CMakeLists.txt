cmake_minimum_required(VERSION 3.22)
project(FoxPlayer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(FoxPlayer
    PRODUCT_NAME        "FoxPlayer"
    BUNDLE_ID           com.greghcarr.foxplayer
    COMPANY_NAME        "Greg Carr"
    ICON_BIG            ""
    MICROPHONE_PERMISSION_ENABLED   FALSE
)

juce_generate_juce_header(FoxPlayer)

target_sources(FoxPlayer PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/audio/PlayQueue.cpp
    src/audio/AudioEngine.cpp
    src/audio/FoxpFile.cpp
    src/audio/AlbumArtExtractor.mm
    src/audio/AlbumArtExtractor.cpp
    src/analysis/BpmDetector.cpp
    src/analysis/KeyDetector.cpp
    src/analysis/AnalysisEngine.cpp
    src/library/LibraryScanner.cpp
    src/library/LibraryTableComponent.cpp
    src/library/PlaylistStore.cpp
    src/ui/TransportBar.cpp
    src/ui/QueueView.cpp
    src/ui/SongInfoEditor.cpp
    src/ui/SidebarComponent.cpp
)

target_include_directories(FoxPlayer PRIVATE src)

target_compile_definitions(FoxPlayer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MIDI_INPUTS=0
    JUCE_USE_MIDI_OUTPUTS=0
    JUCE_DISPLAY_SPLASH_SCREEN=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FoxPlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FoxPlayer,JUCE_VERSION>"
)

target_link_libraries(FoxPlayer
    PRIVATE
        "-framework AVFoundation"
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
