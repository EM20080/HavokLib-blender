cmake_minimum_required(VERSION 3.13)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "X86 architecture is not supported!")
endif()

project(HavokLib VERSION 1)

if(NOT CMAKE_CONFIGURATION_TYPES)
  if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type." FORCE)
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if(MSVC)
  foreach(lang C CXX)
    string(REPLACE "/Zi" "" relwithdebinfo_no_debug "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO "${relwithdebinfo_no_debug}" CACHE STRING "Flags used by the ${lang} compiler during RELWITHDEBINFO builds." FORCE)
  endforeach()
endif()

find_package(Python3 COMPONENTS Development Interpreter)

# ~~~
# Valid targets:
#  - havok-objects: static objects target (enabled by default, set NO_OBJECTS to disable all)
#  - havok: shared library target (BUILD_SHARED_LIBS == ON)
# ~~~

option(PYTHON_MODULE "Build a module for Python 3 (enables OBJECTS_PID)." ON)
set(TOOLSET OFF CACHE BOOL "Build toolset." FORCE)
option(ODR_TEST "Enable ODR testing." OFF)

if(NOT Python3_FOUND AND PYTHON_MODULE)
  message(FATAL_ERROR "Python3 not found.")
endif()

option(OBJECTS_PID "Imply PID for all objects." ${PYTHON_MODULE})

option(NO_OBJECTS "" OFF)
option(CLI "" ${TOOLSET})
option(GLTF "" ${TOOLSET})

if(NOT TOOLSET)
  set(CLI OFF CACHE BOOL "" FORCE)
  set(GLTF OFF CACHE BOOL "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

if(TOOLSET AND BUILD_SHARED_LIBS)
  set(EXPOSE_SYMBOLS spike;gltf;havok;pugixml)
else()
  unset(EXPOSE_SYMBOLS)
endif()

set(TPD_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party)
add_subdirectory(${TPD_PATH}/spike)
include(targetex)
add_subdirectory(classgen)
add_subdirectory(source)

if(TOOLSET)
  add_subdirectory(toolset)
endif()

if(PYTHON_MODULE)
  add_subdirectory(python)
endif()

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  enable_testing()
  add_subdirectory(test)
endif()
