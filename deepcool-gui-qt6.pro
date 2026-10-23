QT += widgets
CONFIG += c++17
TEMPLATE = app
TARGET = deepcool-gui-qt6

SOURCES += \
    main_gui.cpp \
    deepcooldevice.cpp \
    device.cpp

HEADERS += \
    deepcooldevice.h \
    device.h

HEADERS += deepcooldevice.h \
           device.h

INCLUDEPATH += .

LIBS += -lusb-1.0

QMAKE_CXXFLAGS += -Wall -Wextra
