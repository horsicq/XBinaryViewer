cmake_minimum_required(VERSION 3.18)

project(XBinaryViewer VERSION ${X_PROJECT_VERSION} LANGUAGES CXX)

include(../../dep/build_tools/cmake/cpp_standart_setup.cmake)

# Adds the xjs* sources to XPDF and defines USE_PDFJSEMUL (see xpdf.cmake)
option(WITH_PDFJSEMUL "Use PDF JavaScript emulator" ON)
# USE_ARCHIVE gates the archive formats that remain in XArchive (7z, RAR, XZ,
# CAB, LHA, ...) inside XFormats (#ifdef USE_ARCHIVE in xformats.cpp) and
# xtarcompressed.cpp. The sources were already compiled into this target
# through xscanengine.cmake -> xarchives.cmake, but without the define
# xformats.cpp never probed them and those files opened as plain "Binary".
# Must precede the include() chain below: xformats.cmake reads the directory
# COMPILE_DEFINITIONS at include time to decide xarchives.cmake vs xzip.cmake.
add_definitions(-DUSE_ARCHIVE)
add_definitions(-DUSE_YARA)
add_definitions(-DUSE_XSIMD)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
include(../../dep/build_tools/cmake/init_translation.cmake)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../dep/FormatWidgets/XFWidgets/xfwidgets.cmake)
# XDemangleWidget and XVirusTotalWidget now come in via xfwidgets.cmake
include(${CMAKE_CURRENT_LIST_DIR}/../../dep/XOptions/xoptionswidget.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../dep/XAboutWidget/xaboutwidget.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../dep/XStyles/xstyles.cmake)
# Options widgets shown in DialogOptions (Hex/Disasm/Signatures come in via xfwidgets already)
include(${CMAKE_CURRENT_LIST_DIR}/../../dep/XScanEngine/xscanwidgets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../dep/XInfoDB/dialogxinfodbtransferprocess.cmake)

# In-process GUI smoke test (src/gui/smoke). OFF for shipping builds; when ON the
# binary walks every tree node / panel / dialog / control when the XBV_SMOKE
# environment variable names a report directory (see smoke/xbvsmoke.h).
option(XBV_SMOKE_TEST "Compile the in-process GUI smoke-test driver into XBinaryViewer" OFF)

set(PROJECT_SOURCES
        ${XFWIDGETS_SOURCES}
        ${XOPTIONSWIDGET_SOURCES}
        ${XABOUTWIDGET_SOURCES}
        ${XSTYLES_SOURCES}
        ${XSCANWIDGETS_SOURCES}
        ${DIALOGXINFODBTRANSFERPROCESS_SOURCES}
        dialogabout.cpp
        dialogabout.h
        dialogabout.ui
        dialogoptions.cpp
        dialogoptions.h
        dialogoptions.ui
        main_gui.cpp
        guimainwindow.cpp
        guimainwindow.h
        guimainwindow.ui
        rsrc.qrc
)

if(XBV_SMOKE_TEST)
    list(APPEND PROJECT_SOURCES smoke/xbvsmoke.cpp smoke/xbvsmoke.h)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
include(../../dep/build_tools/cmake/create_translation.cmake)
endif()

if(WIN32)
    add_executable(XBinaryViewer
        WIN32
        ${PROJECT_SOURCES}
        ${QM_FILES}
        ../../res/resource_icon.rc
    )
elseif(APPLE)
    add_executable(XBinaryViewer
        MACOSX_BUNDLE
        ${PROJECT_SOURCES}
        ${QM_FILES}
    )

    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../../res/main.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../res/main.icns)

else()
    add_executable(XBinaryViewer
        ${PROJECT_SOURCES}
        ${QM_FILES}
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    if(APPLE)
        include(../../dep/build_tools/cmake/macdeployqt.cmake)
    endif()
endif()

if(XBV_SMOKE_TEST)
    target_compile_definitions(XBinaryViewer PRIVATE XBV_SMOKE_TEST)
    if(WIN32)
        # symbolised crash stacks in the smoke report
        target_link_libraries(XBinaryViewer PRIVATE dbghelp)
    endif()
endif()

target_link_libraries(XBinaryViewer PRIVATE bzip2)
target_link_libraries(XBinaryViewer PRIVATE lzma)
target_link_libraries(XBinaryViewer PRIVATE zlib)
target_link_libraries(XBinaryViewer PRIVATE ppmd)
# libzstd_static is no longer built by XArchive: a symbol-level check showed
# nothing referenced it. zstd support comes from the Algos amalgamations
# (zstddeclib.cpp, zstd_xxhash.cpp, zstdlegacy_v04-v07.cpp) compiled into this
# target through XArchive's source list.
target_link_libraries(XBinaryViewer PRIVATE capstone)
target_link_libraries(XBinaryViewer PRIVATE cppfilt)
target_link_libraries(XBinaryViewer PRIVATE yara)
target_link_libraries(XBinaryViewer PRIVATE xsimd)

target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::Concurrent)
target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::Network)
target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::PrintSupport)
target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::OpenGL)
target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::Svg)
target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::Sql)

if(${QT_VERSION_MAJOR} EQUAL 5)
    target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::Script)
    target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::ScriptTools)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    target_link_libraries(XBinaryViewer PRIVATE Qt${QT_VERSION_MAJOR}::Qml)
endif()

if(WIN32)
    target_link_libraries(XBinaryViewer PRIVATE Wintrust)
    target_link_libraries(XBinaryViewer PRIVATE Crypt32)
endif()

find_package(Threads REQUIRED)
target_link_libraries(XBinaryViewer PRIVATE Threads::Threads)

if(WIN32)
    install (TARGETS XBinaryViewer DESTINATION "./")
    include(../../dep/build_tools/cmake/deploy_qt_windows.cmake)
    include(../../dep/build_tools/cmake/deploy_msvc.cmake)
    include(../../dep/build_tools/cmake/deploy_openssl.cmake)
elseif(APPLE)
    install(TARGETS XBinaryViewer
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install (TARGETS XBinaryViewer DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
