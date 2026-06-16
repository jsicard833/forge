cmake_minimum_required(VERSION 3.23)

project(Forge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(forge INTERFACE)

target_include_directories(forge INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(forge INTERFACE cxx_std_23)

add_library(forge::forge ALIAS forge)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[Forge] Top-level project detected. Building internal tests, benchmarks, and examples.")
    
    enable_testing()

    option(BUILD_EXAMPLES "Build example applications" ON)

    add_subdirectory(tests)
    add_subdirectory(benchmarks)

    if(BUILD_EXAMPLES)
        add_subdirectory(examples)
    endif()
else()
    message(STATUS "[Forge] Sub-project detected (External Library Mode). Skipping internal tests, benchmarks, and examples.")
endif()