@echo off
REM Test runner script for DAFX_2_Daisy_lib
REM Builds and executes unit tests using CMake and Google Test

setlocal enabledelayedexpansion

echo ========================================
echo DAFX_2_Daisy_lib Unit Test Runner
echo ========================================
echo.

REM Set up paths
set "PROJECT_ROOT=%~dp0.."
set "BUILD_DIR=%PROJECT_ROOT%\build"

REM Parse arguments
set "REBUILD=0"
set "VERBOSE=0"
set "FILTER="

:parse_args
if "%~1"=="" goto :end_parse_args
if /i "%~1"=="--rebuild" (
    set "REBUILD=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set "VERBOSE=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--filter" (
    set "FILTER=%~2"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    goto :show_help
)
shift
goto :parse_args
:end_parse_args

REM Create or clean build directory
if "%REBUILD%"=="1" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

if not exist "%BUILD_DIR%" (
    echo Creating build directory...
    mkdir "%BUILD_DIR%"
)

REM Configure with CMake
cd /d "%BUILD_DIR%"
echo.
echo Configuring with CMake...
cmake .. -DBUILD_TESTING=ON
if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

REM Build the project
echo.
echo Building tests...
cmake --build . --config Release
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

REM Run tests
echo.
echo ========================================
echo Running Unit Tests
echo ========================================
echo.

set "TEST_EXE=%BUILD_DIR%\tests\Release\run_tests.exe"
if not exist "%TEST_EXE%" (
    set "TEST_EXE=%BUILD_DIR%\tests\run_tests.exe"
)
if not exist "%TEST_EXE%" (
    set "TEST_EXE=%BUILD_DIR%\tests\Debug\run_tests.exe"
)

if not exist "%TEST_EXE%" (
    echo [ERROR] Test executable not found!
    exit /b 1
)

set "TEST_ARGS="
if "%VERBOSE%"=="1" set "TEST_ARGS=--gtest_print_time=1"
if not "%FILTER%"=="" set "TEST_ARGS=%TEST_ARGS% --gtest_filter=%FILTER%"

"%TEST_EXE%" %TEST_ARGS%
set "TEST_RESULT=%errorlevel%"

echo.
echo ========================================
if "%TEST_RESULT%"=="0" (
    echo All tests PASSED!
) else (
    echo Some tests FAILED!
)
echo ========================================

exit /b %TEST_RESULT%

:show_help
echo Usage: run_tests.cmd [options]
echo.
echo Options:
echo   --rebuild    Clean and rebuild the project
echo   --verbose    Show detailed test output
echo   --filter X   Run only tests matching filter X
echo   --help       Show this help message
echo.
echo Examples:
echo   run_tests.cmd                      Run all tests
echo   run_tests.cmd --rebuild            Clean rebuild and run tests
echo   run_tests.cmd --filter TubeTest.*  Run only Tube tests
echo   run_tests.cmd --verbose            Run with verbose output
exit /b 0
