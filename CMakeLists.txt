cmake_minimum_required(VERSION 3.21)

project(qt_udp_frame_reassembler
    VERSION 1.0.0
    DESCRIPTION "Reusable Qt UDP application-frame reassembly with ordering, duplicate suppression, and timeouts"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Widgets Test)

add_library(udp_frame_reassembler STATIC
    src/reassembly/crc32.cpp
    src/reassembly/crc32.h
    src/reassembly/packetformat.cpp
    src/reassembly/packetformat.h
    src/reassembly/framereassembler.cpp
    src/reassembly/framereassembler.h
)

target_include_directories(udp_frame_reassembler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(udp_frame_reassembler
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

target_compile_definitions(udp_frame_reassembler
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

add_executable(frame_reassembler_demo
    src/demo/main.cpp
    src/demo/mainwindow.cpp
    src/demo/mainwindow.h
    src/demo/packetsimulator.cpp
    src/demo/packetsimulator.h
)

target_link_libraries(frame_reassembler_demo
    PRIVATE
        udp_frame_reassembler
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Widgets
)

if(WIN32)
    set_target_properties(frame_reassembler_demo PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

enable_testing()

add_executable(frame_reassembler_tests
    tests/tst_framereassembler.cpp
    src/demo/packetsimulator.cpp
    src/demo/packetsimulator.h
)

target_link_libraries(frame_reassembler_tests
    PRIVATE
        udp_frame_reassembler
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME frame_reassembler_tests COMMAND frame_reassembler_tests)
