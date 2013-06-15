#-------------------------------------------------
#
# Project created by QtCreator 2013-01-01T19:24:15
#
#-------------------------------------------------

TARGET = installerFomod
TEMPLATE = lib

contains(QT_VERSION, "^5.*") {
  QT += widgets
}

CONFIG += plugins
CONFIG += dll

DEFINES += INSTALLERFOMOD_LIBRARY

SOURCES += installerfomod.cpp \
    fomodinstallerdialog.cpp

HEADERS += installerfomod.h \
    fomodinstallerdialog.h

include(../plugin_template.pri)

FORMS += \
    fomodinstallerdialog.ui

OTHER_FILES += \
    installerfomod.json
