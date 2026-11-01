cmake_minimum_required(VERSION 3.16)
project(rage-jxr LANGUAGES CXX)

# C++20: the decoder relies on arithmetic shifts of negative values, which C++20 defines.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(rage-jxr
    src/main.cpp
    src/entropy.cpp
    src/transform.cpp
    src/pages.cpp
    src/image_out.cpp
)

if(MSVC)
    target_compile_options(rage-jxr PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(rage-jxr PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(rage-jxr PRIVATE -Wall -Wextra)
endif()
