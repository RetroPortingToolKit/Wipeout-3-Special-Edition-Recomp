cmake_minimum_required(VERSION 3.20)

project(clean_aot LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(CLEAN_AOT_COMPONENTS
    cdrom
    compiler
    cpu
    gte
    gpu
    machine
    mdec
    memory
    raster
    render
    sio
    soc
    spu
)

foreach(component IN LISTS CLEAN_AOT_COMPONENTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${component}/CMakeLists.txt")
        add_subdirectory("${component}")
    else()
        message(STATUS "clean-aot component not present yet: ${component}")
    endif()
endforeach()
