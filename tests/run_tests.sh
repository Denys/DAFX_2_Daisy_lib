#!/bin/bash
# Test runner script for DAFX_2_Daisy_lib
# Builds and executes unit tests using CMake and Google Test

set -e

echo "========================================"
echo "DAFX_2_Daisy_lib Unit Test Runner"
echo "========================================"
echo ""

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"

# Default options
REBUILD=0
VERBOSE=0
FILTER=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --rebuild)
            REBUILD=1
            shift
            ;;
        --verbose)
            VERBOSE=1
            shift
            ;;
        --filter)
            FILTER="$2"
            shift 2
            ;;
        --help)
            echo "Usage: run_tests.sh [options]"
            echo ""
            echo "Options:"
            echo "  --rebuild    Clean and rebuild the project"
            echo "  --verbose    Show detailed test output"
            echo "  --filter X   Run only tests matching filter X"
            echo "  --help       Show this help message"
            echo ""
            echo "Examples:"
            echo "  ./run_tests.sh                      Run all tests"
            echo "  ./run_tests.sh --rebuild            Clean rebuild and run tests"
            echo "  ./run_tests.sh --filter TubeTest.*  Run only Tube tests"
            echo "  ./run_tests.sh --verbose            Run with verbose output"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Clean build directory if requested
if [[ $REBUILD -eq 1 ]]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory if it doesn't exist
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Configure with CMake
cd "$BUILD_DIR"
echo ""
echo "Configuring with CMake..."
cmake .. -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release

# Build the project
echo ""
echo "Building tests..."
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Run tests
echo ""
echo "========================================"
echo "Running Unit Tests"
echo "========================================"
echo ""

TEST_EXE="${BUILD_DIR}/tests/run_tests"

if [[ ! -f "$TEST_EXE" ]]; then
    echo "[ERROR] Test executable not found!"
    exit 1
fi

# Build test arguments
TEST_ARGS=""
if [[ $VERBOSE -eq 1 ]]; then
    TEST_ARGS="--gtest_print_time=1"
fi
if [[ -n "$FILTER" ]]; then
    TEST_ARGS="$TEST_ARGS --gtest_filter=$FILTER"
fi

# Run tests and capture result
set +e
"$TEST_EXE" $TEST_ARGS
TEST_RESULT=$?
set -e

echo ""
echo "========================================"
if [[ $TEST_RESULT -eq 0 ]]; then
    echo "All tests PASSED!"
else
    echo "Some tests FAILED!"
fi
echo "========================================"

exit $TEST_RESULT
