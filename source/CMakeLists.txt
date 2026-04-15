file(GLOB_RECURSE CORE_SOURCE_FILES "*.cpp")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/types_data/hk550_skel_types.bin" _HK550_SKEL_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _HK550_SKEL_BYTES "${_HK550_SKEL_HEX}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/types_data/hk550_skel_types_BE.bin" _HK550_SKEL_BE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _HK550_SKEL_BE_BYTES "${_HK550_SKEL_BE_HEX}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/types_data/hk2010_skel_types.bin" _HK2010_SKEL_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _HK2010_SKEL_BYTES "${_HK2010_SKEL_HEX}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/types_data/hk2012_skel_types.bin" _HK2012_SKEL_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _HK2012_SKEL_BYTES "${_HK2012_SKEL_HEX}")
configure_file(types_embed.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/types_embed.cpp" @ONLY)
list(APPEND CORE_SOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/types_embed.cpp")

add_library(havok-interface INTERFACE)
target_include_directories(havok-interface INTERFACE ../include)
target_link_libraries(havok-interface INTERFACE spike-interface)

if(NOT NO_OBJECTS)
  build_target(
    NAME
    havok-objects
    TYPE
    OBJECT
    SOURCES
    ${CORE_SOURCE_FILES}
    PROPERTIES
    POSITION_INDEPENDENT_CODE ${OBJECTS_PID}
    INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    LINKS
    spike-objects
    havok-interface
    NO_VERINFO
    NO_PROJECT_H)

    if(NOT MSVC)
      target_compile_options(havok-objects PRIVATE -fvisibility=hidden)
    endif()
    target_expose_defs(havok HK_EXPORT HK_IMPORT)

  add_library(havok STATIC)
  target_sources(havok PRIVATE $<TARGET_OBJECTS:havok-objects>)
  target_link_libraries(havok PUBLIC havok-interface spike pugixml)
  install(TARGETS havok ARCHIVE DESTINATION lib)
endif()

if(BUILD_SHARED_LIBS)
  build_target(
    NAME
    havok
    TYPE
    SHARED
    SOURCES
    ${CORE_SOURCE_FILES}
    INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    LINKS
    spike
    pugixml
    havok-interface
    START_YEAR
    2017
    AUTHOR
    "Lukas Cone"
    NO_PROJECT_H)

  target_compile_definitions(havok PRIVATE HK_EXPORT)

  if(WIN32 OR MINGW)
    set_target_properties(havok PROPERTIES SUFFIX ${PROJECT_VERSION_MAJOR}.dll
                                           PREFIX "")
  else()
    set_target_properties(havok PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
  endif()

  target_compile_definitions(havok INTERFACE HK_IMPORT)

  install(
    TARGETS havok
    LIBRARY NAMELINK_SKIP DESTINATION $<IF:$<BOOL:${MINGW}>,bin,lib>
    RUNTIME DESTINATION bin)
endif()
