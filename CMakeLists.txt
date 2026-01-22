cmake_minimum_required(VERSION 3.10)
project(obu-sdk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)  # Qt MOC automatski

# SDK Library
add_library(obu-sdk STATIC
    src/serial.cpp
    src/epdi.cpp
    src/mboard.cpp
    src/terminal.cpp
    src/qr_scanner.cpp
    src/nfc_reader.cpp
)

target_include_directories(obu-sdk PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include/obu
)

# CLI Test - uvijek buildaj
add_executable(cli_test examples/cli_test.cpp)
target_link_libraries(cli_test PRIVATE obu-sdk)

# Qt GUI - opcionalno
option(BUILD_QT_GUI "Build Qt GUI example" OFF)

if(BUILD_QT_GUI)
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    
    add_executable(qt_gui examples/qt_gui.cpp)
    target_link_libraries(qt_gui PRIVATE obu-sdk Qt5::Widgets pthread)
    
    # Ne može biti static s Qt-om
endif()