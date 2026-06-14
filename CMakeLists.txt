cmake_minimum_required(VERSION 3.21)

project(qt_npcap_udp_fallback
    VERSION 1.0.0
    DESCRIPTION "Hybrid Qt UDP receiver with an optional Npcap raw-frame fallback"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(qt-npcap-udp-fallback
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/hybridudpreceiver.cpp
    src/hybridudpreceiver.h
    src/npcapreceiver.cpp
    src/npcapreceiver.h
    src/udppacketparser.cpp
    src/udppacketparser.h
)

target_link_libraries(qt-npcap-udp-fallback PRIVATE
    Qt6::Widgets
    Qt6::Network
)

target_include_directories(qt-npcap-udp-fallback PRIVATE src)

target_compile_definitions(qt-npcap-udp-fallback PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(WIN32)
    target_compile_definitions(qt-npcap-udp-fallback PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(qt-npcap-udp-fallback PRIVATE ws2_32)
    set_target_properties(qt-npcap-udp-fallback PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
