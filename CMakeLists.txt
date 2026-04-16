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
    ICON_BIG            "${CMAKE_SOURCE_DIR}/resources/app-icon.png"
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
    src/ui/AnalysisLogWindow.cpp
    src/ui/PreferencesWindow.cpp
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

# JUCE runs juceaide at CMake *configure* time to bake resources/app-icon.png
# into FoxPlayer_artefacts/JuceLibraryCode/Icon.icns, which the build then just
# copies into the bundle. If only the PNG changes (no configure re-run, no
# CMakeLists.txt edit), neither icns gets refreshed. Force it at build time:
# invoke juceaide ourselves to regenerate the icns, then delete the stale copy
# in the bundle so JUCE recopies the fresh one.
set(APP_ICON_PNG   ${CMAKE_CURRENT_SOURCE_DIR}/resources/app-icon.png)
set(APP_ICON_STAMP ${CMAKE_CURRENT_BINARY_DIR}/.app-icon.stamp)
set(APP_ICON_ICNS  ${CMAKE_CURRENT_BINARY_DIR}/FoxPlayer_artefacts/JuceLibraryCode/Icon.icns)

add_custom_command(
    OUTPUT  ${APP_ICON_STAMP}
    DEPENDS ${APP_ICON_PNG}
    COMMAND $<TARGET_FILE:juce::juceaide> macicon
            "${APP_ICON_ICNS}" "${APP_ICON_PNG}"
    COMMAND ${CMAKE_COMMAND} -E rm -f
            "${CMAKE_CURRENT_BINARY_DIR}/FoxPlayer_artefacts/$<CONFIG>/FoxPlayer.app/Contents/Resources/Icon.icns"
    COMMAND ${CMAKE_COMMAND} -E touch ${APP_ICON_STAMP}
    COMMENT "app-icon.png changed - regenerating Icon.icns via juceaide"
    VERBATIM
)

add_custom_target(FoxPlayerIconRefresh DEPENDS ${APP_ICON_STAMP})
add_dependencies(FoxPlayer FoxPlayerIconRefresh)

juce_add_binary_data(FoxPlayerIcons
    SOURCES
        resources/icons/play-fill.svg
        resources/icons/pause-fill.svg
        resources/icons/skip-back-fill.svg
        resources/icons/skip-forward-fill.svg
        resources/icons/shuffle-fill.svg
        resources/icons/repeat-fill.svg
        resources/icons/repeat-once-fill.svg
        resources/icons/speaker-none-fill.svg
        resources/icons/speaker-low-fill.svg
        resources/icons/speaker-high-fill.svg
        resources/icons/speaker-x-fill.svg
        resources/icons/queue-fill.svg
        resources/icons/caret-double-right-fill.svg
        resources/fonts/Foxwhelp-Regular.ttf
        resources/app-icon.png
)

target_link_libraries(FoxPlayer
    PRIVATE
        FoxPlayerIcons
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
