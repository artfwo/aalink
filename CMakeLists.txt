cmake_minimum_required(VERSION 3.16)

project(aalink)

find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/pybind11)
pybind11_add_module(aalink src/aalink.cpp)

include(${CMAKE_CURRENT_SOURCE_DIR}/ext/link/AbletonLinkConfig.cmake)
target_link_libraries(aalink PUBLIC Ableton::Link)

install(TARGETS aalink LIBRARY DESTINATION .)
