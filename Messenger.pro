QT += core
CONFIG += qt c++17 dll
TEMPLATE = lib
TARGET = Messenger
DESTDIR = $$PWD/libs

DEFINES += MESSAGING_LIBRARY

HEADERS += \
    Messenger.h

SOURCES += \
    Messenger.cpp

INCLUDEPATH += .
