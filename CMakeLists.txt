cmake_minimum_required (VERSION 3.1)
project (coro VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    message(STATUS "msvc optional: /utf-8")   
endif(MSVC)

add_library(
    ${PROJECT_NAME} 
    STATIC
    src/io_service.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC include/coro)
