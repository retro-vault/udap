# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.25)
project(udap LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source CMake builds are disabled.\n"
    "Use: cmake -S . -B build"
  )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static libraries for everything
set(BUILD_SHARED_LIBS OFF)

# We want a debug build by default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Fetch all external dependencies ---
include(FetchContent)
if(NOT DEFINED FETCHCONTENT_UPDATES_DISCONNECTED)
  set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
      "Disable FetchContent network update checks during local builds" FORCE)
endif()

# Use the actual z80ex repo by lipro
set(Z80EX_BUILD_DASM ON CACHE BOOL "Build z80ex disassembler" FORCE)
FetchContent_Declare(
  z80ex
  GIT_REPOSITORY https://github.com/lipro/z80ex.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(z80ex)

message(STATUS "Available targets:")
get_property(all_targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
foreach(t ${all_targets})
  message(STATUS "  target: ${t}")
endforeach()

FetchContent_Declare(
  sockpp
  GIT_REPOSITORY https://github.com/fpagliughi/sockpp.git
  GIT_TAG        v0.8.0
)
FetchContent_MakeAvailable(sockpp)

FetchContent_Declare(
  structopt
  GIT_REPOSITORY https://github.com/p-ranav/structopt.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(structopt)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Add subdirectories ---
add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(ext)

option(BUILD_TESTS "Whether or not to build tests." ON)
if (BUILD_TESTS)
  message(STATUS "Building tests...")
  enable_testing()
  add_subdirectory(tests)
endif()
