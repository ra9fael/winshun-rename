# CMake generated Testfile for 
# Source directory: D:/dev/github/winshun-dev/ra9fael.rename
# Build directory: D:/dev/github/winshun-dev/ra9fael.rename/build-qt
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[rename_core]=] "D:/dev/github/winshun-dev/ra9fael.rename/build-qt/tests/rename_tests.exe")
set_tests_properties([=[rename_core]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/dev/github/winshun-dev/ra9fael.rename/CMakeLists.txt;185;add_test;D:/dev/github/winshun-dev/ra9fael.rename/CMakeLists.txt;0;")
add_test([=[rename_e2e]=] "D:/dev/github/winshun-dev/ra9fael.rename/build-qt/rename_e2e.exe")
set_tests_properties([=[rename_e2e]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/dev/github/winshun-dev/ra9fael.rename/CMakeLists.txt;198;add_test;D:/dev/github/winshun-dev/ra9fael.rename/CMakeLists.txt;0;")
add_test([=[core_purity]=] "C:/Program Files/PowerShell/7/pwsh.exe" "-NoProfile" "-File" "D:/dev/github/winshun-dev/ra9fael.rename/tools/check_core_purity.ps1")
set_tests_properties([=[core_purity]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/dev/github/winshun-dev/ra9fael.rename/CMakeLists.txt;206;add_test;D:/dev/github/winshun-dev/ra9fael.rename/CMakeLists.txt;0;")
