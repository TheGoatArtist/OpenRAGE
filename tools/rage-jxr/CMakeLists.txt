cmake_minimum_required(VERSION 3.16)
project(rage-jxr LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The decoder as a library, so the engine can link it too (add_subdirectory this folder and link
# ragejxr). Its headers are under src/.
add_library(ragejxr STATIC
    src/entropy.cpp
    src/transform.cpp
    src/pages.cpp
    src/megatexture.cpp
    src/dct.cpp
)
target_include_directories(ragejxr PUBLIC src)

add_executable(rage-jxr
    src/main.cpp
    src/image_out.cpp
)
target_link_libraries(rage-jxr PRIVATE ragejxr)

# C++20 for these targets only: the decoder relies on arithmetic shifts of negative values, which
# C++20 defines. The public headers also compile as C++17, which the engine uses.
foreach(target ragejxr rage-jxr)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
