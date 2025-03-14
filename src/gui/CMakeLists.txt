cmake_minimum_required(VERSION 3.16)

project(xbinaryviewer VERSION ${X_PROJECT_VERSION} LANGUAGES CXX)

include(../../dep/build_tools/cmake/cpp_standart_setup.cmake)

add_definitions(-DUSE_DEX)
add_definitions(-DUSE_PDF)
add_definitions(-DUSE_ARCHIVE)
add_definitions(-DUSE_YARA)

if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
include(../../dep/build_tools/cmake/init_translation.cmake)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../dep/FormatWidgets/AbstractWidgets/xformatwidget.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../dep/XAboutWidget/xaboutwidget.cmake)

set(PROJECT_SOURCES
        ${XFORMATWIDGET_SOURCES}
        ${XABOUTWIDGET_SOURCES}
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

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
include(../../dep/build_tools/cmake/create_translation.cmake)
endif()

if(WIN32)
    add_executable(${PROJECT_NAME}
        WIN32
        ${PROJECT_SOURCES}
        ${QM_FILES}
        ../../res/resource_icon.rc
    )
elseif(APPLE)
    add_executable(${PROJECT_NAME}
        MACOSX_BUNDLE
        ${PROJECT_SOURCES}
        ${QM_FILES}
    )
else()
    add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        ${QM_FILES}
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(APPLE)
    include(../../dep/build_tools/cmake/macdeployqt.cmake)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE bzip2)
target_link_libraries(${PROJECT_NAME} PRIVATE lzma)
target_link_libraries(${PROJECT_NAME} PRIVATE zlib)
target_link_libraries(${PROJECT_NAME} PRIVATE capstone)
target_link_libraries(${PROJECT_NAME} PRIVATE cppfilt)
target_link_libraries(${PROJECT_NAME} PRIVATE yara)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Concurrent)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Network)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::PrintSupport)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::OpenGL)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Svg)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Sql)

if(${QT_VERSION_MAJOR} EQUAL 5)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Script)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::ScriptTools)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Qml)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE Wintrust)
    target_link_libraries(${PROJECT_NAME} PRIVATE Crypt32)
endif()

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

if(WIN32)
    install (TARGETS ${PROJECT_NAME} DESTINATION "./")
    install (DIRECTORY ../../dep/Detect-It-Easy/db DESTINATION "./" OPTIONAL)
    install (DIRECTORY ../../dep/Detect-It-Easy/db_custom DESTINATION "./" OPTIONAL)
    install (DIRECTORY ../../dep/XInfoDB/info DESTINATION "./" OPTIONAL)
    install (DIRECTORY ../../dep/XStyles/qss DESTINATION "./" OPTIONAL)
    install (DIRECTORY ../../dep/XYara/yara_rules DESTINATION "./" OPTIONAL)
    install (DIRECTORY ../../dep/signatures/signatures DESTINATION "./" OPTIONAL)
    install (FILES ${QM_FILES} DESTINATION "./lang" OPTIONAL)
    include(../../dep/build_tools/cmake/deploy_qt_windows.cmake)
    include(../../dep/build_tools/cmake/deploy_msvc.cmake)
    include(../../dep/build_tools/cmake/deploy_openssl.cmake)
elseif(APPLE)
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install (DIRECTORY ../../dep/Detect-It-Easy/db DESTINATION "${PROJECT_NAME}.app/Contents/Resources" OPTIONAL)
    install (DIRECTORY ../../dep/Detect-It-Easy/db_custom DESTINATION "${PROJECT_NAME}.app/Contents/Resources" OPTIONAL)
    install (DIRECTORY ../../dep/XInfoDB/info DESTINATION "${PROJECT_NAME}.app/Contents/Resources" OPTIONAL)
    install (DIRECTORY ../../dep/XStyles/qss DESTINATION "${PROJECT_NAME}.app/Contents/Resources" OPTIONAL)
    install (DIRECTORY ../../dep/XYara/yara_rules DESTINATION "${PROJECT_NAME}.app/Contents/Resources" OPTIONAL)
    install (DIRECTORY ../../dep/signatures/signatures DESTINATION "${PROJECT_NAME}.app/Contents/Resources" OPTIONAL)
    install (FILES ${QM_FILES} DESTINATION DESTINATION "${PROJECT_NAME}.app/Contents/Resources/Lang" OPTIONAL)
else()
    install (TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
    install (DIRECTORY ../../dep/Detect-It-Easy/db DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} OPTIONAL)
    install (DIRECTORY ../../dep/Detect-It-Easy/db_custom DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} OPTIONAL)
    install (DIRECTORY ../../dep/XInfoDB/info DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} OPTIONAL)
    install (DIRECTORY ../../dep/XStyles/qss DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} OPTIONAL)
    install (DIRECTORY ../../dep/XYara/yara_rules DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} OPTIONAL)
    install (DIRECTORY ../../dep/signatures/signatures DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME} OPTIONAL)
    install (FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/lang OPTIONAL)
endif()
