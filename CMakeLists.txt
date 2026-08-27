cmake_minimum_required(VERSION 3.20)
project(ctd_manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ccache -- optional, no-op if not installed (older dev-base images predate it).
# Set before add_subdirectory() so it also covers uuv_common/uuv_interfaces'
# targets, not just this project's own. See xl300-dev-base/Dockerfile for where
# it's installed and each devcontainer.json for the shared "uuv-ccache" volume
# that makes the cache persist (and share) across container rebuilds/repos.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Fast DDS package name differs by version line: the ENTIRE 2.x line's CMake project is
# literally named 'fastrtps' (umeshwalkar/xl300-dev-base:0.1.0 pins Fast-DDS 2.14.x LTS).
# Resolved here (not inside uuv_common/uuv_interfaces) so both subdirectories can just
# reuse the target already in scope -- see their own CMakeLists.txt comments.
find_package(fastdds QUIET CONFIG)
if(fastdds_FOUND)
  set(FASTDDS_TARGET fastdds)
else()
  find_package(fastrtps REQUIRED CONFIG)
  set(FASTDDS_TARGET fastrtps)
endif()
find_package(fastcdr REQUIRED)
find_package(Threads REQUIRED)

# nlohmann/json — prefer the apt-installed package (umeshwalkar/xl300-dev-base's
# Dockerfile installs nlohmann-json3-dev); fall back to FetchContent if that image
# predates it.
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found via find_package -- fetching via FetchContent")
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# uuv_common (Logger/Transport/TransportConfig/DdsNode) and uuv_interfaces
# (xl300_dds_types -- generated code for the FULL xl300-dds-v2 contract) --
# git submodules, not vendored copies. Run `git submodule update --init --recursive`
# first if either directory is empty.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/uuv_common/CMakeLists.txt")
  message(FATAL_ERROR "uuv_common/ is empty -- run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/uuv_interfaces/generated")
  message(FATAL_ERROR
    "uuv_interfaces/generated/ not found -- run: git submodule update --init --recursive\n"
    "(uuv_interfaces ships its generated code committed; this app does not run fastddsgen itself.)")
endif()
add_subdirectory(uuv_common)
add_subdirectory(uuv_interfaces)

set(CTD_MANAGER_LIB_SRCS Config.cpp CtdManager.cpp)

add_executable(ctd_manager main.cpp ${CTD_MANAGER_LIB_SRCS})
target_include_directories(ctd_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ctd_manager PRIVATE uuv_common xl300_dds_types nlohmann_json::nlohmann_json)

install(TARGETS ctd_manager RUNTIME DESTINATION /opt/xl300/bin)

# probe: standalone dev/debug subscriber (see tools/probe.cpp) to verify ctd_manager is
# actually publishing over DDS. Not installed/shipped -- build-tree tool only.
add_executable(probe tools/probe.cpp)
target_include_directories(probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(probe PRIVATE uuv_common xl300_dds_types)
