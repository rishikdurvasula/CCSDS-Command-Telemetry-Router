cmake_minimum_required(VERSION 3.20)
project(ccsds_router LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
enable_testing()

add_library(ccsds_core
    src/crc16.cpp
    src/space_packet.cpp
    src/lossy_channel.cpp
)

target_include_directories(ccsds_core PUBLIC include)

if (MSVC)
    target_compile_options(ccsds_core PRIVATE /W4 /WX)
else()
    target_compile_options(ccsds_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(test_space_packet
    tests/test_space_packet.cpp
)
target_link_libraries(test_space_packet PRIVATE ccsds_core)

add_executable(udp_sender src/udp_sender.cpp)
target_link_libraries(udp_sender PRIVATE ccsds_core)

add_executable(udp_receiver src/udp_receiver.cpp)
target_link_libraries(udp_receiver PRIVATE ccsds_core)



add_test(NAME test_space_packet COMMAND test_space_packet)