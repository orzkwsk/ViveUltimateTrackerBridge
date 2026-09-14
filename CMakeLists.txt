cmake_minimum_required(VERSION 3.20)
project(ViveUltimateTrackerBridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vut_backend STATIC
    src/protocol.cpp
    src/backend.cpp
)
target_include_directories(vut_backend PUBLIC include)
if(WIN32)
    target_compile_definitions(vut_backend PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(vut_backend PRIVATE hid setupapi)
endif()

add_executable(vut_probe tools/vut_probe.cpp)
target_link_libraries(vut_probe PRIVATE vut_backend)

include(CTest)
if(BUILD_TESTING)
    add_executable(vut_protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(vut_protocol_tests PRIVATE vut_backend)
    add_test(NAME protocol COMMAND vut_protocol_tests)
endif()
