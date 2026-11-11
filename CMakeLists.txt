cmake_minimum_required(VERSION 3.22)

project(Viceroy
    VERSION 0.0.1
    DESCRIPTION "Clean-room engine that plays the 1994 colonial strategy game from your own files"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VCR_BUILD_APP "Build the game executable (needs SDL3)" ON)
option(VCR_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(VCR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VcrWarnings)

# Engine core: platform independent, no SDL. Everything that can be tested
# without a screen lives here.
add_subdirectory(src/core)

if(VCR_BUILD_APP)
    include(VcrSDL3)
    add_subdirectory(src/app)
endif()

if(VCR_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()
