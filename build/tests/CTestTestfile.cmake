# CMake generated Testfile for 
# Source directory: C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests
# Build directory: C:/Users/denko/Claude/DAFX_2_Daisy_lib/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(UnitTests "C:/Users/denko/Claude/DAFX_2_Daisy_lib/build/tests/Debug/run_tests.exe")
  set_tests_properties(UnitTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;54;add_test;C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(UnitTests "C:/Users/denko/Claude/DAFX_2_Daisy_lib/build/tests/Release/run_tests.exe")
  set_tests_properties(UnitTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;54;add_test;C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(UnitTests "C:/Users/denko/Claude/DAFX_2_Daisy_lib/build/tests/MinSizeRel/run_tests.exe")
  set_tests_properties(UnitTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;54;add_test;C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(UnitTests "C:/Users/denko/Claude/DAFX_2_Daisy_lib/build/tests/RelWithDebInfo/run_tests.exe")
  set_tests_properties(UnitTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;54;add_test;C:/Users/denko/Claude/DAFX_2_Daisy_lib/tests/CMakeLists.txt;0;")
else()
  add_test(UnitTests NOT_AVAILABLE)
endif()
subdirs("../_deps/googletest-build")
