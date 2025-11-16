QT += core gui testlib
CONFIG += qt console c++17
TARGET = MessengerTests

SOURCES += \
    tst_Messenger.cpp

HEADERS += \
    ../Messenger.h \
    tst_Messenger.h

INCLUDEPATH += ..

# 链接到共享库输出目录（根据 Debug/Release 切换）
win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../libs -lMessenger
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../libs -lMessenger
