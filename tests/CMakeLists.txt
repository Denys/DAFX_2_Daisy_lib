# Unit Tests for DAFX_2_Daisy_lib
# Using Google Test framework

# Use FetchContent to automatically download and build Google Test
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Make available
FetchContent_MakeAvailable(googletest)

# Legacy test source files.
# NOTE: Several tests have API/build mismatches and remain visible here until
# the wider repository test debt is handled separately.
set(TEST_SOURCES
    # Effects (working tests only)
    test_tube.cpp
    test_lowshelving.cpp
    test_highshelving.cpp
    test_peakfilter.cpp
    test_vibrato.cpp
    test_ringmod.cpp
    test_stereopan.cpp
    test_noisegate.cpp
    test_wahwah.cpp
    # test_tonestack.cpp  # TODO: Fix SetMid/GetMid API mismatch
    # Utilities
    test_circularbuffer.cpp
    # test_envelopefollower.cpp  # TODO: Add M_PI include
    test_princarg.cpp
    test_windows.cpp
    test_fft_handler.cpp
    # test_xcorr.cpp  # TODO: Add M_PI include
    # Analysis (Phase 2)
    # test_yin.cpp  # TODO: Add M_PI include
    # Spectral (Phase 2 - Track B)
    test_robotization.cpp
    test_whisperization.cpp
    # Spectral (Track B Sync 3)
    test_spectral_filter.cpp
    test_phase_vocoder.cpp
    test_crosstalk_canceller.cpp
    # Effects (Phase 2 - Track A Sync 3) - TODO: implement these
    # test_fdn_reverb.cpp
    # test_sola.cpp
)

# Existing aggregate suite. Kept intact so legacy failures remain visible.
add_executable(unit_tests ${TEST_SOURCES})
target_link_libraries(unit_tests
    gtest
    gtest_main
    dafx_daisysp
)
add_test(NAME UnitTests COMMAND unit_tests)

# Scoped Pedal & Synth Harness acceptance target. It remains independent from
# unrelated legacy test/example failures while new portable nodes are migrated.
add_executable(pedal_harness_tests
    test_dsp_contract.cpp
    test_digital_delay_node.cpp
    test_fractional_delay.cpp
    test_fractional_delay_extremes.cpp
)
target_link_libraries(pedal_harness_tests
    gtest
    gtest_main
    dafx_daisysp
)
add_test(NAME PedalHarnessContractTests COMMAND pedal_harness_tests)

# Host-side characterization emits a stable CSV schema. Static measurements are
# deterministic; the CPU rows are observational and validated separately.
add_executable(fractional_delay_characterization
    fractional_delay_characterization.cpp
)
target_link_libraries(fractional_delay_characterization
    dafx_daisysp
)
add_test(NAME FractionalDelayCharacterization
         COMMAND fractional_delay_characterization)

# Enable testing
enable_testing()
