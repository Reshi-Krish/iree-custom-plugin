cmake_minimum_required(VERSION 3.26)
project(iree_custom_plugin C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# Defines BUILD_TESTING (default ON) and calls enable_testing().
include(CTest)

add_subdirectory(hardware_emulator)
add_subdirectory(hardware_kernels)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
