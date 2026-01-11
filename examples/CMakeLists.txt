# Hardware-Agnostic Examples for DAFX_2_Daisy_lib

# Example source files
set(EXAMPLE_SOURCES
    example_guitar_amp.cpp
    example_parametric_eq.cpp
    example_modulation.cpp
)

# Create example executables
foreach(example_source ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${example_source} NAME_WE)
    add_executable(${example_name} ${example_source})
    target_link_libraries(${example_name} dafx_daisysp)
endforeach()
