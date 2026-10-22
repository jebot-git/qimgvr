cmake_minimum_required(VERSION 3.11)
project(player_mpv
    DESCRIPTION "video player widget for qimgv (using mpv)"
    LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

# only export CreatePlayerWidget function
ADD_DEFINITIONS(-DQIMGV_PLAYER_MPV_LIBRARY)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Mpv REQUIRED IMPORTED_TARGET mpv)
    pkg_check_modules(BitmapFontconfig REQUIRED IMPORTED_TARGET fontconfig)
    find_library(DE_XR_MPV_LIBRARY NAMES mpv libmpv.so.2 PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../../mpvr/build" NO_DEFAULT_PATH REQUIRED)
    find_library(DE_XR_BITMAP_ASS NAMES ass libass.so.9 PATHS
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../libass/build-bitmap/libass"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../fltk-experiment/libass/build-bitmap/libass"
        "/opt/fltk-vr/lib/bitmap" NO_DEFAULT_PATH REQUIRED)
endif()

include(GNUInstallDirs)

add_library(player_mpv MODULE
    src/videoplayer.cpp
    src/mpvwidget.cpp
    src/videoplayermpv.cpp
    src/qthelper.hpp)

target_compile_features(player_mpv PRIVATE cxx_std_11)

if(WIN32)
    target_link_libraries(player_mpv PRIVATE
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Mpv)
else()
    target_link_libraries(player_mpv PRIVATE
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets "${DE_XR_MPV_LIBRARY}" PkgConfig::BitmapFontconfig)
    target_include_directories(player_mpv PRIVATE ${Mpv_INCLUDE_DIRS})
    get_filename_component(DE_XR_ASS_DIR "${DE_XR_BITMAP_ASS}" DIRECTORY)
    get_filename_component(DE_XR_MPV_DIR "${DE_XR_MPV_LIBRARY}" DIRECTORY)
    target_link_options(player_mpv PRIVATE -Wl,--disable-new-dtags)
    set_target_properties(player_mpv PROPERTIES BUILD_RPATH "${DE_XR_ASS_DIR};${DE_XR_MPV_DIR}" INSTALL_RPATH "$ORIGIN/..;$ORIGIN/../bitmap;/opt/fltk-vr/lib;/opt/fltk-vr/lib/bitmap")
endif()

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    target_link_libraries(player_mpv PRIVATE Qt${QT_VERSION_MAJOR}::OpenGLWidgets)
endif()

set_target_properties(player_mpv PROPERTIES
    CXX_EXTENSIONS OFF
    PREFIX "")

target_include_directories(player_mpv PRIVATE src)

if(WIN32)
    target_include_directories(player_mpv PRIVATE
        ${MPV_DIR}/include)
    target_link_directories(player_mpv PRIVATE
        ${MPV_DIR}/lib/$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,x86_64,i686>)
endif()

if(WIN32)
    install(TARGETS player_mpv LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins")
else()
    install(TARGETS player_mpv LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qimgvr")
endif()
