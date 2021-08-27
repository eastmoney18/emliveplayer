cmake_minimum_required(VERSION 3.4.1)

if (CMAKE_HOST_UNIX)
  set(lib_build_DIR $ENV{HOME}/tmp)
else ()
  set(lib_build_DIR $ENV{TEMP})
endif ()
set(CMAKE_VERBOSE_MAKEFILE on)

set(lib_src_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(MAKE_DIRECTORY ${lib_build_DIR})

add_subdirectory(${lib_src_DIR}/ijkplayer ${lib_build_DIR}/ijkplayer)