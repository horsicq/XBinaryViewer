set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DUSE_DEX)
add_definitions(-DUSE_PDF)
add_definitions(-DUSE_ARCHIVE)

# XCppfilt/XCapstone/XArchive/XYara/xsimd targets already exist from the
# add_subdirectory() calls in ../CMakeLists.txt (shared with the gui target).
include(${CMAKE_CURRENT_LIST_DIR}/../../../_mylibs/Formats/xformats.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../_mylibs/Formats/xfmodel.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../_mylibs/XOptions/xoptions.cmake)

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
endif()

add_executable(xbinaryviewerc
    ${XFORMATS_SOURCES}
    ${XFMODEL_SOURCES}
    ${XOPTIONS_SOURCES}
    main_console.cpp
)

target_include_directories(xbinaryviewerc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# xformats.h declares QComboBox-taking helpers, and XArchive's decompression
# code needs XOptions' XThreadObject — both pull in Widgets even though this
# is a console app that never shows one.
target_link_libraries(xbinaryviewerc PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)

target_link_libraries(xbinaryviewerc PRIVATE bzip2)
target_link_libraries(xbinaryviewerc PRIVATE lzma)
target_link_libraries(xbinaryviewerc PRIVATE zlib)
target_link_libraries(xbinaryviewerc PRIVATE ppmd)
target_link_libraries(xbinaryviewerc PRIVATE capstone)
target_link_libraries(xbinaryviewerc PRIVATE cppfilt)

if(WIN32)
    target_link_libraries(xbinaryviewerc PRIVATE Wintrust)
    target_link_libraries(xbinaryviewerc PRIVATE Crypt32)
endif()

if(UNIX AND NOT APPLE)
    install(TARGETS xbinaryviewerc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    install(TARGETS xbinaryviewerc RUNTIME DESTINATION ".")
endif()
