cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# CI sets FANWAVE_VERSION from the release tag (v1.2.3 -> 1.2.3).
if(DEFINED ENV{FANWAVE_VERSION})
    set(PROJECT_VER "$ENV{FANWAVE_VERSION}")
else()
    set(PROJECT_VER "0.2.0-dev")
endif()
project(fanwave)
