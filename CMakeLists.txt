cmake_minimum_required(VERSION 3.10)
project(obu-sdk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# CLI Test - static linked
add_executable(qt_gui examples/qt_gui.cpp)
target_link_libraries(qt_gui PRIVATE obu-sdk)
set_target_properties(qt_gui PROPERTIES LINK_FLAGS "-static")