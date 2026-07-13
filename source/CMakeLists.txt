file(GLOB_RECURSE CORE_SOURCE_FILES "*.cpp")

set(HAVOK_2016_METADATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(HAVOK_2016_METADATA ${HAVOK_2016_METADATA_DIR}/havok_2016_metadata.inl)
add_custom_command(
  OUTPUT ${HAVOK_2016_METADATA}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${HAVOK_2016_METADATA_DIR}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/format_new_metadata.py
          ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/format_new_metadata.bin
          ${HAVOK_2016_METADATA}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/format_new_metadata.py
    ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/format_new_metadata.bin
  VERBATIM)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/format_new_write.cpp
                            PROPERTIES OBJECT_DEPENDS ${HAVOK_2016_METADATA})

set(HAVOK_300_METADATA ${HAVOK_2016_METADATA_DIR}/havok_300_metadata.inl)
add_custom_command(
  OUTPUT ${HAVOK_300_METADATA}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${HAVOK_2016_METADATA_DIR}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/havok300metadata.py
          ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/havok300metadata.bin
          ${HAVOK_300_METADATA}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/havok300metadata.py
    ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/havok300metadata.bin
  VERBATIM)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/format_old_write.cpp
                            PROPERTIES OBJECT_DEPENDS ${HAVOK_300_METADATA})

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
    ${HAVOK_2016_METADATA_DIR}
    ${HAVOK_CLASSGEN_DIR}
    ${HAVOK_CLASSGEN_DIR}/custom
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
  if(TARGET spike-objects)
    target_sources(havok PRIVATE $<TARGET_OBJECTS:spike-objects>)
  endif()
  if(TARGET pugixml-objects)
    target_sources(havok PRIVATE $<TARGET_OBJECTS:pugixml-objects>)
  endif()
  target_link_libraries(havok PUBLIC havok-interface pugixml-interface)
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
    ${HAVOK_2016_METADATA_DIR}
    ${HAVOK_CLASSGEN_DIR}
    ${HAVOK_CLASSGEN_DIR}/custom
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
