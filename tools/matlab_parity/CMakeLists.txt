# Standalone parity tool for the MATLAB DIGI model (matlab/multidelay_digi, test T9).
# Not part of the root build: cmake -S tools/matlab_parity -B tools/matlab_parity/build
cmake_minimum_required(VERSION 3.16)
project(matlab_parity LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(dump_digital_delay_node dump_digital_delay_node.cpp)
target_include_directories(dump_digital_delay_node PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Bit-exact parity needs one rounding per float operation, as MATLAB single does.
    target_compile_options(dump_digital_delay_node PRIVATE -ffp-contract=off)
endif()
