cmake_minimum_required(VERSION 3.10)
project(obu-sdk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(obu-sdk STATIC
    src/transport/serial.cpp
    src/protocol/epdi.cpp
    src/devices/mboard.cpp
    src/devices/terminal.cpp
    src/devices/qr_scanner.cpp
    src/obu/nfc_reader.cpp
    src/validator/nfc_reader.cpp
    src/common/helpers.cpp
)

target_include_directories(obu-sdk PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(obu-sdk PUBLIC pthread)

add_executable(cli_test examples/cli_test.cpp)
target_link_libraries(cli_test PRIVATE obu-sdk)

add_executable(corvus_nfc_example examples/corvus_nfc_example.cpp)
target_link_libraries(corvus_nfc_example PRIVATE obu-sdk)

add_executable(nfc_example examples/nfc_example.cpp)
target_link_libraries(nfc_example PRIVATE obu-sdk)

option(BUILD_QT_GUI "Build Qt GUI example" OFF)

if(BUILD_QT_GUI)
    find_package(Qt5 COMPONENTS Widgets Network REQUIRED)
    
    add_executable(corvus_nfc_qt examples/corvus_nfc_qt_example.cpp)
    target_link_libraries(corvus_nfc_qt PRIVATE obu-sdk Qt5::Widgets Qt5::Network)
endif()