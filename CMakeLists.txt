# DAFX_2_Daisy_lib CMake Build Configuration
# Portable DSP algorithm library derived from DAFX textbook
# Targeting Daisy Seed embedded platform with hardware independence

cmake_minimum_required(VERSION 3.16)
project(dafx_daisysp VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-unused-parameter
    )
elseif(MSVC)
    add_compile_options(
        /W4
        /WX
        /wd4100  # Disable unused parameter warning
    )
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Source files - Effects
set(EFFECTS_SOURCES
    src/effects/tube.cpp
    src/effects/wahwah.cpp
    src/effects/tonestack.cpp
)

# Source files - Filters
set(FILTERS_SOURCES
    src/filters/lowshelving.cpp
    src/filters/highshelving.cpp
    src/filters/peakfilter.cpp
)

# Source files - Dynamics
set(DYNAMICS_SOURCES
    src/dynamics/noisegate.cpp
)

# Source files - Modulation
set(MODULATION_SOURCES
    src/modulation/vibrato.cpp
    src/modulation/ringmod.cpp
)

# Source files - Spatial
set(SPATIAL_SOURCES
    src/spatial/stereopan.cpp
)

# Source files - Analysis
set(ANALYSIS_SOURCES
    # Add analysis implementations here
)

# Source files - Utility
set(UTILITY_SOURCES
    # Add utility implementations here
)

# All library sources
set(LIBRARY_SOURCES
    ${EFFECTS_SOURCES}
    ${FILTERS_SOURCES}
    ${DYNAMICS_SOURCES}
    ${MODULATION_SOURCES}
    ${SPATIAL_SOURCES}
    ${ANALYSIS_SOURCES}
    ${UTILITY_SOURCES}
)

# Create the library
add_library(dafx_daisysp STATIC ${LIBRARY_SOURCES})

# Set library properties
set_target_properties(dafx_daisysp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "src/effects/tube.h"
)

# Installation rules
install(TARGETS dafx_daisysp
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include/dafx_daisysp
)

# Install all header files
install(DIRECTORY src/
    DESTINATION include/dafx_daisysp
    FILES_MATCHING PATTERN "*.h"
)

# Testing option
option(BUILD_TESTING "Build unit tests" ON)

if(BUILD_TESTING)
    enable_testing()
    
    # Add test subdirectory if it exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

# Examples option
option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    # Add examples subdirectory if it exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
        add_subdirectory(examples)
    endif()
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "DAFX_2_Daisy_lib Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Testing: ${BUILD_TESTING}")
message(STATUS "  Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "")
