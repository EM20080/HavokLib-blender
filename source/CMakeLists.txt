file(GLOB_RECURSE CORE_SOURCE_FILES "*.cpp")

set(METADATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(TYPES ${METADATA_DIR}/types.inl)
set(NEW_METADATA ${METADATA_DIR}/metadata_new.inl)
add_custom_command(
  OUTPUT ${TYPES} ${NEW_METADATA}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${METADATA_DIR}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/metadata.py
          ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/metadata.bin
          ${TYPES}
          ${NEW_METADATA}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/metadata.py
    ${CMAKE_CURRENT_SOURCE_DIR}/../classgen/metadata.bin
  VERBATIM)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/format_new_write.cpp
                            PROPERTIES OBJECT_DEPENDS ${NEW_METADATA})
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/format_old_write.cpp
                            PROPERTIES OBJECT_DEPENDS ${TYPES})

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
    ${METADATA_DIR}
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
    ${METADATA_DIR}
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
