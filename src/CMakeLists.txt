cmake_minimum_required(VERSION 3.10)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)

if(${QT_VERSION_MAJOR} EQUAL 5)
    if(${QT_VERSION} VERSION_GREATER_EQUAL 5.6.0)
        find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent Network Script ScriptTools PrintSupport OpenGL Svg Sql LinguistTools)
    else()
        find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent Network Script ScriptTools PrintSupport OpenGL Svg Sql)
    endif()
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Concurrent Network PrintSupport OpenGL Svg Sql Qml LinguistTools)
endif()

file (STRINGS ${CMAKE_CURRENT_LIST_DIR}/../release_version.txt X_PROJECT_VERSION)
project(XBinaryViewer VERSION ${X_PROJECT_VERSION} LANGUAGES CXX)

set(X_COMPANYNAME "ntinfo")
set(X_PROJECTNAME "XBinaryViewer")
set(X_MAINTAINER "horsicq@gmail.com")
set(X_DESCRIPTION "XBinaryViewer is a binary viewer")
set(X_HOMEPAGE "http://ntinfo.biz")
set(X_ORIGINAL_FILENAME "xbinaryviewer")

set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.xbinaryviewer)

include(../dep/build_tools/cmake/deploy_init.cmake)

message(STATUS X_PROJECT_ARCH: ${X_PROJECT_ARCH})

add_subdirectory(../dep/XCppfilt XCppfilt)
add_subdirectory(../dep/XCapstone XCapstone)
add_subdirectory(../dep/XArchive XArchive)
add_subdirectory(../dep/XYara XYara)
add_subdirectory(../dep/Formats/xsimd XSIMD)
add_subdirectory(gui gui_xbinaryviewer)
