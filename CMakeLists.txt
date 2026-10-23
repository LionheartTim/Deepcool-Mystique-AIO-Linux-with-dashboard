cmake_minimum_required(VERSION 3.16)
project(deepcool-gui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 🔍 Qt6 — Core voor types, Gui voor QImage/QPainter, Widgets voor de GUI, DBus voor tray/notificaties
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus)

# 🔌 libusb voor DeepCool USB communicatie
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# Zorg dat de compiler onze headers kan vinden
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Alle bronbestanden
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main_gui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/deepcooldevice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/deepcooldevice.cpp
)

add_executable(deepcool-gui ${SOURCES})

target_link_libraries(deepcool-gui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    PkgConfig::LIBUSB
)
