cmake_minimum_required(VERSION 3.22)
project(Stylus VERSION 0.1.0)

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

# StylusCore: portable analysis + library + audio-data layer.
# Links only the GUI-free JUCE modules, so it can be reused on iOS as a
# static library underneath a SwiftUI app. No source in this target may
# depend on juce_graphics or juce_gui_basics.
set(STYLUS_CORE_SOURCES
    src/audio/PlayQueue.cpp
    src/audio/StylFile.cpp
    src/analysis/BpmDetector.cpp
    src/analysis/KeyDetector.cpp
    src/analysis/AnalysisEngine.cpp
    src/analysis/AppleMusicLookup.cpp
    src/library/LibraryScanner.cpp
    src/library/LibraryCache.cpp
    src/library/PlaylistStore.cpp
)

if (APPLE)
    list(APPEND STYLUS_CORE_SOURCES src/audio/AlbumArtExtractor.mm)
else()
    list(APPEND STYLUS_CORE_SOURCES src/audio/AlbumArtExtractor_Win.cpp)
endif()

add_library(StylusCore STATIC ${STYLUS_CORE_SOURCES})

target_include_directories(StylusCore PUBLIC src)

target_link_libraries(StylusCore
    PUBLIC
        juce::juce_core
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

juce_add_gui_app(Stylus
    PRODUCT_NAME        "Stylus"
    BUNDLE_ID           com.greghcarr.stylus
    COMPANY_NAME        "Greg Carr"
    ICON_BIG            "${CMAKE_SOURCE_DIR}/resources/app-icon.png"
    MICROPHONE_PERMISSION_ENABLED   FALSE
)

juce_generate_juce_header(Stylus)

set(STYLUS_APP_SOURCES
    src/Main.cpp
    src/MainComponent.cpp
    src/audio/AudioEngine.cpp
    src/audio/AlbumArtExtractor.cpp
    src/library/LibraryTableComponent.cpp
    src/ui/TransportBar.cpp
    src/ui/QueueView.cpp
    src/ui/SongInfoEditor.cpp
    src/ui/SidebarComponent.cpp
    src/ui/AnalysisLogWindow.cpp
    src/ui/PreferencesWindow.cpp
)

if (APPLE)
    list(APPEND STYLUS_APP_SOURCES
        src/ui/StatusBarItem.mm
        src/ui/MacWindowHelper.mm
        src/ui/NowPlayingBridge.mm
        src/ui/RecordSpinnerLayer.mm
    )
else()
    list(APPEND STYLUS_APP_SOURCES
        src/ui/StatusBarItem_Win.cpp
        src/ui/MacWindowHelper_Win.cpp
        src/ui/NowPlayingBridge_Win.cpp
        src/ui/RecordSpinnerLayer_Win.cpp
    )
endif()

target_sources(Stylus PRIVATE ${STYLUS_APP_SOURCES})

target_include_directories(Stylus PRIVATE src)

target_compile_definitions(Stylus PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MIDI_INPUTS=0
    JUCE_USE_MIDI_OUTPUTS=0
    JUCE_DISPLAY_SPLASH_SCREEN=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Stylus,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Stylus,JUCE_VERSION>"
)

if (APPLE)
    # JUCE runs juceaide at CMake *configure* time to bake resources/app-icon.png
    # into Stylus_artefacts/JuceLibraryCode/Icon.icns, which the build then just
    # copies into the bundle. If only the PNG changes (no configure re-run, no
    # CMakeLists.txt edit), neither icns gets refreshed. Force it at build time:
    # invoke juceaide ourselves to regenerate the icns, then delete the stale copy
    # in the bundle so JUCE recopies the fresh one.
    set(APP_ICON_PNG   ${CMAKE_CURRENT_SOURCE_DIR}/resources/app-icon.png)
    set(APP_ICON_STAMP ${CMAKE_CURRENT_BINARY_DIR}/.app-icon.stamp)
    set(APP_ICON_ICNS  ${CMAKE_CURRENT_BINARY_DIR}/Stylus_artefacts/JuceLibraryCode/Icon.icns)

    add_custom_command(
        OUTPUT  ${APP_ICON_STAMP}
        DEPENDS ${APP_ICON_PNG}
        COMMAND $<TARGET_FILE:juce::juceaide> macicon
                "${APP_ICON_ICNS}" "${APP_ICON_PNG}"
        COMMAND ${CMAKE_COMMAND} -E rm -f
                "${CMAKE_CURRENT_BINARY_DIR}/Stylus_artefacts/$<CONFIG>/Stylus.app/Contents/Resources/Icon.icns"
        COMMAND ${CMAKE_COMMAND} -E touch ${APP_ICON_STAMP}
        COMMENT "app-icon.png changed - regenerating Icon.icns via juceaide"
        VERBATIM
    )

    add_custom_target(StylusIconRefresh DEPENDS ${APP_ICON_STAMP})
    add_dependencies(Stylus StylusIconRefresh)
endif()

juce_add_binary_data(StylusIcons
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
        resources/icons/pushpin-simple-fill.svg
        resources/icons/music-notes-fill.svg
        resources/icons/list-bullets-fill.svg
        resources/icons/user-fill.svg
        resources/icons/vinyl-record-fill.svg
        resources/icons/tag-fill.svg
        resources/icons/caret-double-right-fill.svg
        resources/icons/microphone-fill.svg
        resources/app-icon.png
)

target_link_libraries(Stylus
    PRIVATE
        StylusCore
        StylusIcons
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if (APPLE)
    target_link_libraries(Stylus PRIVATE
        "-framework AVFoundation"
        "-framework MediaPlayer"
        "-framework IOKit"
    )
endif()
