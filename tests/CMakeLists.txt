qt_add_executable(test-udp-packet-parser
    test_udppacketparser.cpp
    ../src/udppacketparser.cpp
    ../src/udppacketparser.h
)

target_include_directories(test-udp-packet-parser PRIVATE ../src)
target_link_libraries(test-udp-packet-parser PRIVATE Qt6::Test Qt6::Network)

add_test(NAME udp-packet-parser COMMAND test-udp-packet-parser)
