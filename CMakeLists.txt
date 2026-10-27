cmake_minimum_required(VERSION 3.21)

project(StringArtStudio
    VERSION 0.1.0
    DESCRIPTION "String art generator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STRINGART_BUILD_APP "Build the Qt Quick application" ON)
option(STRINGART_BUILD_TESTS "Build the Qt Test suite" ON)
option(STRINGART_BUILD_BENCHMARKS "Build the benchmark tool" ON)

set(STRINGART_QT_COMPONENTS Core Gui Concurrent)
if(STRINGART_BUILD_APP)
    list(APPEND STRINGART_QT_COMPONENTS Qml Quick QuickControls2 Widgets)
endif()
if(STRINGART_BUILD_TESTS)
    list(APPEND STRINGART_QT_COMPONENTS Test)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS ${STRINGART_QT_COMPONENTS})
if(STRINGART_BUILD_APP)
    # Optional: spoken pin numbers in winding mode.
    find_package(Qt6 QUIET OPTIONAL_COMPONENTS TextToSpeech)
endif()

qt_standard_project_setup(REQUIRES 6.8)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(src)

if(STRINGART_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(STRINGART_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

add_subdirectory(examples)
