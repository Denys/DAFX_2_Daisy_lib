# Unit Tests for DAFX_2_Daisy_lib
# Using Google Test framework

# Use FetchContent to automatically download and build Google Test
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.12.1
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Make available
FetchContent_MakeAvailable(googletest)

# Test source files
set(TEST_SOURCES
    test_tube.cpp
    # Add more test files as modules are implemented
    # test_lowshelving.cpp
    # test_highshelving.cpp
    # test_peakfilter.cpp
    # test_vibrato.cpp
    # test_ringmod.cpp
    # test_stereopan.cpp
    # test_noisegate.cpp
    # test_wahwah.cpp
    # test_tonestack.cpp
)

# Create test executable
add_executable(run_tests ${TEST_SOURCES})

# Link against Google Test and the library
target_link_libraries(run_tests
    gtest
    gtest_main
    dafx_daisysp
)

# Add test to CTest
add_test(NAME UnitTests COMMAND run_tests)

# Enable testing
enable_testing()
