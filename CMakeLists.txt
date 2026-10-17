cmake_minimum_required(VERSION 3.21)

project(pineapple-chapter-marker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets LinguistTools)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
)

qt_add_executable(pineapple-chapter-marker WIN32
    src/main.cpp

    src/core/Chapter.h
    src/core/TimeUtils.h
    src/core/TimeUtils.cpp
    src/core/ChapterDocument.h
    src/core/ChapterDocument.cpp
    src/core/MediaProbe.h
    src/core/MediaProbe.cpp
    src/core/MediaWriter.h
    src/core/MediaWriter.cpp
    src/core/WaveformPeaks.h
    src/core/WaveformPeaks.cpp
    src/core/WaveformBuilder.h
    src/core/WaveformBuilder.cpp

    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/TimelineController.h
    src/ui/TimelineController.cpp
    src/ui/PlaybackController.h
    src/ui/PlaybackController.cpp
    src/ui/ChapterCommands.h
    src/ui/WaveformView.h
    src/ui/WaveformView.cpp
    src/ui/OverviewBar.h
    src/ui/OverviewBar.cpp
    src/ui/ChapterTableModel.h
    src/ui/ChapterTableModel.cpp
    src/ui/TimestampTextDialog.h
    src/ui/TimestampTextDialog.cpp
)

target_include_directories(pineapple-chapter-marker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pineapple-chapter-marker PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    PkgConfig::FFMPEG
)

# ---- i18n (English source copy; Chinese embedded via a .ts translation) ----
set(PCM_TS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/translations/pineapple-chapter-marker_zh_CN.ts")
set(PCM_QM_FILE "${CMAKE_CURRENT_BINARY_DIR}/pineapple-chapter-marker_zh_CN.qm")

set_source_files_properties("${PCM_QM_FILE}" PROPERTIES
    QT_RESOURCE_ALIAS "pineapple-chapter-marker_zh_CN.qm")

add_custom_command(
    OUTPUT "${PCM_QM_FILE}"
    COMMAND Qt6::lrelease -silent "${PCM_TS_FILE}" -qm "${PCM_QM_FILE}"
    DEPENDS "${PCM_TS_FILE}"
    COMMENT "lrelease zh_CN translation"
    VERBATIM
)
qt_add_resources(pineapple-chapter-marker "pcm_translations"
    PREFIX "/i18n"
    FILES "${PCM_QM_FILE}"
)

# Refresh translation sources manually: cmake --build build --target update_translations
add_custom_target(update_translations
    COMMAND Qt6::lupdate -no-obsolete src -ts "${PCM_TS_FILE}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "lupdate: refresh TS file"
    VERBATIM
)

# CLI self-check tool: prints a file's format/streams/chapters for headless regression checks
qt_add_executable(probe-test
    tests/ProbeTestMain.cpp
    src/core/MediaProbe.cpp
    src/core/TimeUtils.cpp
)
target_include_directories(probe-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(probe-test PRIVATE
    Qt6::Core
    PkgConfig::FFMPEG
)

# CLI self-check tool: end-to-end test of the save pipeline (remux + chapters + validation + replace)
qt_add_executable(save-test
    tests/SaveTestMain.cpp
    src/core/MediaWriter.cpp
    src/core/TimeUtils.cpp
)
target_include_directories(save-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(save-test PRIVATE
    Qt6::Core
    PkgConfig::FFMPEG
)
