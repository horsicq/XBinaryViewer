QT       += core gui widgets sql

TARGET = xbinaryviewer

macx {
    TARGET = XBinaryViewer
}

TEMPLATE = app

CONFIG += c++11

SOURCES += \
        guimainwindow.cpp \
        main_gui.cpp \
        dialogoptions.cpp \
        dialogabout.cpp

HEADERS += \
        ../global.h \
        dialogoptions.h \
        dialogabout.h \
        guimainwindow.h

FORMS += \
        dialogoptions.ui \
        dialogabout.ui \
        guimainwindow.ui

include(../build.pri)

XCONFIG += use_extrabuttons

!contains(XCONFIG, binarywidget) {
    XCONFIG += binarywidget
    include(../../_mylibs/FormatWidgets/Binary/binarywidget.pri)
}

!contains(XCONFIG, xoptions) {
    XCONFIG += xoptions
    include(../../_mylibs/XOptions/xoptions.pri)
}

win32 {
    RC_ICONS = ../icons/main.ico
    CONFIG -= embed_manifest_exe
    QMAKE_MANIFEST = windows.manifest.xml
    VERSION = 0.01.0.0
    QMAKE_TARGET_COMPANY = NTInfo
    QMAKE_TARGET_PRODUCT = XBinaryViewer
    QMAKE_TARGET_DESCRIPTION = XBinaryViewer is a Binary file viewer/editor.
    QMAKE_TARGET_COPYRIGHT = horsicq@gmail.com
}
macx {
    ICON = ../icons/main.icns
}

RESOURCES += \
    rsrc.qrc

DISTFILES += \
    ../CMakeLists.txt \
    ../LICENSE \
    ../README.md \
    ../changelog.txt \
    ../release_version.txt \
    CMakeLists.txt
