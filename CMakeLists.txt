cmake_minimum_required(VERSION 3.21)

project(pineapple-chapter-marker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    # Source files and string literals are UTF-8 (…, ✓, ≠, ×); MSVC assumes
    # the legacy ANSI codepage without this flag.
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets LinguistTools)

# FFmpeg via the FindFFmpeg.cmake that ships with the Qt6 package.
# This must come after find_package(Qt6): Qt6Config prepends its cmake
# directory to CMAKE_MODULE_PATH, which is where the module is found.
# The module uses pkg-config only as an optional hint and otherwise
# falls back to plain find_path/find_library (also honours FFMPEG_DIR).
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL)

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
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
)

qt_add_translations(pineapple-chapter-marker
    TS_FILES translations/pineapple-chapter-marker_zh_CN.ts
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
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
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
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
)

install(TARGETS pineapple-chapter-marker RUNTIME DESTINATION bin)
