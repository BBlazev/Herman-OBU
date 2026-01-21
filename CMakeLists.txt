cmake_minimum_required(VERSION 3.10) # 18.04 ima stariji CMake
project(obu-sdk)

set(CMAKE_CXX_STANDARD 17)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

add_library(obu-sdk STATIC
    src/serial.cpp
    src/epdi.cpp
    src/mboard.cpp
    src/terminal.cpp
    src/qr_scanner.cpp
)
target_include_directories(obu-sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/obu)

# Buildaj samo jedan binarni fajl radi jednostavnosti
add_executable(obu_gui examples/qt_gui.cpp)
target_link_libraries(obu_gui PRIVATE obu-sdk Qt5::Widgets)
set_target_properties(obu_gui PROPERTIES AUTOMOC ON)