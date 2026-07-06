cmake_minimum_required(VERSION 3.16)

project(OrderBookEngines LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ORDERBOOK_ENGINE_SOURCES
    src/bitmap_engine/ladder.cpp
    src/bitmap_engine/order_map.cpp
    src/map_engine/orderbook.cpp
)

add_library(orderbook_options INTERFACE)

target_compile_features(orderbook_options INTERFACE cxx_std_20)

target_include_directories(orderbook_options INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
)

function(add_orderbook_executable target_name)
    add_executable(${target_name}
        ${ARGN}
        ${ORDERBOOK_ENGINE_SOURCES}
    )

    target_link_libraries(${target_name} PRIVATE orderbook_options)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()

include(CTest)

if(BUILD_TESTING)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/bitmap_engine_test.cpp")
        add_orderbook_executable(
            bitmap_engine_test
            tests/bitmap_engine_test.cpp
        )

        add_test(
            NAME bitmap_engine_test
            COMMAND bitmap_engine_test
        )
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/map_engine_test.cpp")
        add_orderbook_executable(
            map_engine_test
            tests/map_engine_test.cpp
        )

        add_test(
            NAME map_engine_test
            COMMAND map_engine_test
        )
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential_test.cpp")
        add_orderbook_executable(
            differential_test
            tests/differential_test.cpp
        )

        add_test(
            NAME differential_test
            COMMAND differential_test
        )
    endif()
endif()

option(ORDERBOOK_BUILD_LATENCY "Build latency benchmark executables" ON)
option(ORDERBOOK_NATIVE_ARCH "Use -march=native for latency benchmarks" OFF)

if(ORDERBOOK_BUILD_LATENCY)
    add_orderbook_executable(
        latency_map
        benchmarks/latency.cpp
    )

    target_compile_definitions(latency_map PRIVATE
        LATENCY_ENGINE_MAP
    )

    add_orderbook_executable(
        latency_bitmap
        benchmarks/latency.cpp
    )

    target_compile_definitions(latency_bitmap PRIVATE
        LATENCY_ENGINE_BITMAP
    )

    # GCC 13 on your system is hitting an internal compiler error during
    # optimized RTL passes.
    #
    # Work around that compiler bug by compiling the latency executables
    # with lower optimization when using GCC < 14.
    #
    # If you switch to Clang or GCC 14+, this block will not apply.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
        target_compile_options(latency_map PRIVATE -O1)
        target_compile_options(latency_bitmap PRIVATE -O1)
    endif()

    if(ORDERBOOK_NATIVE_ARCH AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(latency_map PRIVATE -march=native)
        target_compile_options(latency_bitmap PRIVATE -march=native)
    endif()
endif()