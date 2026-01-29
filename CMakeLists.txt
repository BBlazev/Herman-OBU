cmake_minimum_required(VERSION 3.10)
project(obu-sdk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(obu-sdk STATIC
    src/serial.cpp
    src/epdi.cpp
    src/mboard.cpp
    src/terminal.cpp
    src/qr_scanner.cpp
    src/obu/nfc_reader.cpp
    src/validator/nfc_reader.cpp
    src/helpers.cpp
)

target_include_directories(obu-sdk PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(obu-sdk PUBLIC pthread)

add_executable(cli_test examples/cli_test.cpp)
target_link_libraries(cli_test PRIVATE obu-sdk)

add_executable(corvus_nfc_example examples/nfc_qt.cpp)
target_link_libraries(corvus_nfc_example PRIVATE obu-sdk)

option(BUILD_QT_GUI "Build Qt GUI example" OFF)

if(BUILD_QT_GUI)
    find_package(Qt5 COMPONENTS Widgets Network REQUIRED)
    
    add_executable(corvus_nfc_qt examples/nfc_qt.cpp)
    target_link_libraries(corvus_nfc_qt PRIVATE obu-sdk Qt5::Widgets Qt5::Network)
endif()