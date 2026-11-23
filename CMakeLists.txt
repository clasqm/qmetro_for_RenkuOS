cmake_minimum_required(VERSION 3.16)
project(qmetro VERSION 0.7.1 LANGUAGES CXX)

# qMetro keeps its runtime data folders inside a source-tree bin/ directory,
# so an in-source build would stage over them and `make clean` would delete
# real project files. Require the documented out-of-source build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Out-of-source build required: mkdir build && cd build && cmake ..")
endif()

# Haiku's qt5_devel installs the CMake config packages here; harmless elsewhere.
if(EXISTS /boot/system/lib/cmake)
  list(APPEND CMAKE_PREFIX_PATH /boot/system/lib/cmake)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(ZLIB REQUIRED)

set(QMETRO_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/settings.cpp
    src/details.cpp
    src/completer.cpp
    src/mobileui.cpp
    src/flickcharm.cpp
    src/swipegesturerecognizer.cpp
    src/ini/inisettings.cpp
    src/zip/zipglobal.cpp
    src/zip/zip.cpp
    src/zip/unzip.cpp
    src/mapview/mapview.cpp
    src/mapview/maproute.cpp
    src/mapview/mapgraphics.cpp
    src/mapview/mapnode.cpp
    src/mapview/mapedge.cpp
    src/mapview/maplayer.cpp
    src/mapview/mapelement.cpp
    # update module (QMETRO_HAVE_UPDATE); unrelated to the qmake QT -= network bug
    src/update/update.cpp
    src/update/download.cpp
)

set(QMETRO_HEADERS
    src/main.h
    src/mainwindow.h
    src/settings.h
    src/details.h
    src/about.h
    src/completer.h
    src/mobileui.h
    src/flickcharm.h
    src/swipegesturerecognizer.h
    src/ini/inisettings.h
    src/zip/zipglobal.h
    src/zip/zip.h
    src/zip/zip_p.h
    src/zip/zipentry_p.h
    src/zip/unzip.h
    src/zip/unzip_p.h
    src/zlib/zlib.h
    src/zlib/zconf.h
    src/mapview/mapview.h
    src/mapview/maproute.h
    src/mapview/mapgraphics.h
    src/mapview/mapnode.h
    src/mapview/mapedge.h
    src/mapview/maplayer.h
    src/mapview/mapelement.h
    src/update/update.h
    src/update/download.h
)

set(QMETRO_FORMS
    ui/mainwindow.ui
    ui/settings.ui
    ui/details.ui
)

add_executable(qmetro
    ${QMETRO_SOURCES}
    ${QMETRO_HEADERS}
    ${QMETRO_FORMS}
    rc/resource.qrc
)

target_include_directories(qmetro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(qmetro PRIVATE
    APPNAME="qMetro"
    VERSION="0.7.1"
    QMETRO_PLATFORM="Source"
    QMETRO_HAVE_UPDATE=1)

target_link_libraries(qmetro PRIVATE
    Qt5::Widgets
    Qt5::Gui
    Qt5::Core
    Qt5::Network
    ZLIB::ZLIB)

# --- runtime layout ---------------------------------------------------------
# The program resolves map/, locale/ and skin/ relative to its own directory
# (appPath = qApp->applicationDirPath(), src/mainwindow.cpp), so the executable
# and a staged copy of those folders live together in <build>/bin. Nothing is
# written into the source tree, so `make clean` removes the binary along with
# the staged data instead of leaving it behind in bin/.
set(QMETRO_RUNTIME_DIR ${CMAKE_BINARY_DIR}/bin)

set_target_properties(qmetro PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${QMETRO_RUNTIME_DIR})

set(QMETRO_DATA_DIRS map locale skin)

set(QMETRO_STAGE_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory ${QMETRO_RUNTIME_DIR})
foreach(data_dir ${QMETRO_DATA_DIRS})
  list(APPEND QMETRO_STAGE_COMMANDS
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_CURRENT_SOURCE_DIR}/bin/${data_dir}
              ${QMETRO_RUNTIME_DIR}/${data_dir})
endforeach()

add_custom_target(stage_data ${QMETRO_STAGE_COMMANDS}
    COMMENT "Staging map/, locale/ and skin/ beside the executable")
add_dependencies(qmetro stage_data)

set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${QMETRO_RUNTIME_DIR})
