@echo off
setlocal

rem Update submodules
git submodule update --init

rem Run the CMake command, need to install vcpkg first
cmake --preset=vcpkg

rem Wait for user input before closing the window
pause

endlocal