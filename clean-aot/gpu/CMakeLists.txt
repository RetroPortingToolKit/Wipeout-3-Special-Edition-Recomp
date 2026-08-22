cmake_minimum_required(VERSION 3.20)

project(clean_aot_gpu LANGUAGES CXX)

option(CLEAN_AOT_GPU_BUILD_TESTS "Build the standalone GPU tests" ON)
option(CLEAN_AOT_GPU_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers" OFF)

add_library(clean_aot_gpu STATIC
  src/gpu.cpp
)

target_compile_features(clean_aot_gpu PUBLIC cxx_std_20)
target_include_directories(clean_aot_gpu
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
  target_compile_options(clean_aot_gpu PRIVATE /W4 /WX)
else()
  target_compile_options(clean_aot_gpu PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
  )
endif()

if(CLEAN_AOT_GPU_ENABLE_SANITIZERS AND NOT MSVC)
  target_compile_options(clean_aot_gpu PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer
    -fno-sanitize-recover=all
  )
  target_link_options(clean_aot_gpu PRIVATE
    -fsanitize=address,undefined -fno-sanitize-recover=all
  )
endif()

include(CTest)

if(CLEAN_AOT_GPU_BUILD_TESTS)
  add_executable(clean_aot_gpu_tests
    tests/gpu_tests.cpp
  )
  target_link_libraries(clean_aot_gpu_tests PRIVATE clean_aot_gpu)
  target_compile_features(clean_aot_gpu_tests PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(clean_aot_gpu_tests PRIVATE /W4 /WX)
  else()
    target_compile_options(clean_aot_gpu_tests PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
    )
  endif()
  if(CLEAN_AOT_GPU_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(clean_aot_gpu_tests PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer
      -fno-sanitize-recover=all
    )
    target_link_options(clean_aot_gpu_tests PRIVATE
      -fsanitize=address,undefined -fno-sanitize-recover=all
    )
  endif()
  add_test(NAME clean_aot_gpu_tests COMMAND clean_aot_gpu_tests)
endif()
