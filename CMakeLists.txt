# qimgv root
cmake_minimum_required(VERSION 3.13)
project(qimgvr
    VERSION 1.2.1
    HOMEPAGE_URL "https://github.com/jebot-git/qimgvr"
    LANGUAGES CXX)

set(DE_XR_TOOLKIT "FLTK" CACHE STRING "Default UI toolkit: FLTK or Qt6")
set_property(CACHE DE_XR_TOOLKIT PROPERTY STRINGS FLTK Qt6)
if(DE_XR_TOOLKIT STREQUAL "FLTK")
  include(CTest)
  add_subdirectory(fltk)
  return()
elseif(NOT DE_XR_TOOLKIT STREQUAL "Qt6")
  message(FATAL_ERROR "Unknown DE_XR_TOOLKIT")
endif()

message(STATUS "Build configuration: " ${CMAKE_BUILD_TYPE})

# check for gcc
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
        CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    message(FATAL_ERROR "\n!!! THIS PROJECT REQUIRES GCC 9.0 OR LATER !!!\n")
endif()

# AUTOMOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# USER OPTIONS
# Usage: cmake -DVIDEO_SUPPORT=ON [...]
option(EXIV2 "For reading/writing exif tags" ON)
option(VIDEO_SUPPORT "Enable video support" ON)
option(OPENCV_SUPPORT "Enable HQ scaling via OpenCV" ON)
option(KDE_SUPPORT "Support blur when using KDE" ON)
option(USE_QT5 "Build with Qt5 if available" OFF)

if(UNIX AND NOT APPLE)
    set(QT_EXTERN_PATH "" CACHE STRING "Tell compile external QT path, example: (/opt/Qt/6.2.0/gcc_64)")
    string(COMPARE EQUAL "${QT_EXTERN_PATH}" "" result)
    if (NOT result)
        set(CMAKE_INSTALL_RPATH "${QT_EXTERN_PATH}/lib")
    endif()
endif()

# FIND PACKAGES
if(USE_QT5) # Try Qt5 first if USE_QT5 is set
    find_package(Qt5 COMPONENTS Widgets QUIET)
    if(Qt5_FOUND)
        find_package(QT NAMES Qt5 REQUIRED COMPONENTS Core Widgets Svg PrintSupport)
    else()
        find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets Svg PrintSupport OpenGLWidgets)
    endif()
else() # Try Qt6 otherwise
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets Svg PrintSupport OpenGLWidgets)
    else()
        find_package(QT NAMES Qt5 REQUIRED COMPONENTS Core Widgets Svg PrintSupport)
    endif()
endif()

message(STATUS "Qt Version: " ${QT_VERSION})
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Svg PrintSupport OpenGLWidgets)
else()
    find_package(Qt${QT_VERSION_MAJOR} 5.12 REQUIRED COMPONENTS Core Widgets Svg PrintSupport)
endif()

if(EXIV2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Exiv2 REQUIRED IMPORTED_TARGET exiv2)
endif()

if(KDE_SUPPORT)
    find_package(KF6WindowSystem REQUIRED)
endif()

if(OPENCV_SUPPORT)
    find_package(PkgConfig REQUIRED)
    find_package(OpenCV REQUIRED core imgproc)
endif()

##############################################################

add_subdirectory(qimgv)

option(SBS_TESTS "Build stereo canvas and asynchronous conversion tests" OFF)
option(MENU_TESTS "Build native application menu tests" OFF)
if(SBS_TESTS OR MENU_TESTS)
    enable_testing()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
endif()
if(MENU_TESTS)
    add_executable(test_applicationmenu qimgv/tests/test_applicationmenu.cpp
        qimgv/gui/applicationmenu.cpp)
    target_include_directories(test_applicationmenu PRIVATE qimgv)
    target_compile_features(test_applicationmenu PRIVATE cxx_std_17)
    target_link_libraries(test_applicationmenu PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets)
    add_test(NAME applicationmenu COMMAND test_applicationmenu)
    set_tests_properties(applicationmenu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
if(SBS_TESTS)
    add_executable(test_sbs qimgv/tests/test_sbs.cpp
        qimgv/components/sbscontroller.cpp qimgv/gui/viewers/stereowindow.cpp)
    target_include_directories(test_sbs PRIVATE qimgv)
    target_compile_features(test_sbs PRIVATE cxx_std_17)
    target_link_libraries(test_sbs PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets)
    add_test(NAME sbs COMMAND test_sbs)
    set_tests_properties(sbs PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(VIDEO_SUPPORT)
    add_subdirectory(plugins/player_mpv)
    if(SBS_TESTS)
        add_executable(test_player qimgv/tests/test_player.cpp qimgv/gui/viewers/videoplayer.cpp)
        target_include_directories(test_player PRIVATE qimgv)
        target_compile_features(test_player PRIVATE cxx_std_17)
        target_link_libraries(test_player PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets)
        target_compile_definitions(test_player PRIVATE PLAYER_PLUGIN="$<TARGET_FILE:player_mpv>")
        add_dependencies(test_player player_mpv)
        add_test(NAME mpv_playback COMMAND test_player)
        set_tests_properties(mpv_playback PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb")
    endif()
endif()

include(GNUInstallDirs)

install(FILES docs/man/qimgvr.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if(SBS_TESTS OR MENU_TESTS)
    add_executable(test_domeappearance tests/domeappearance-test.cpp)
    target_include_directories(test_domeappearance PRIVATE qimgv)
    target_compile_definitions(test_domeappearance PRIVATE DOME_FONT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fonts/ttyp0-13.bdf")
    target_link_libraries(test_domeappearance PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME bitmap-font-appearance COMMAND test_domeappearance -platform offscreen)
endif()
