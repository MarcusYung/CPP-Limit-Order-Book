cmake_minimum_required(VERSION 3.16)
project(MySTLOB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(orderbook_lib
    src/orderbook.cpp
)

target_include_directories(orderbook_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(orderbook_test
    src/main.cpp
)

target_link_libraries(orderbook_test PRIVATE orderbook_lib)