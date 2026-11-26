@echo off
setlocal

pushd "%~dp0" || exit /b 1
git submodule update --init && cmake --preset=vcpkg
set "configure_result=%errorlevel%"
popd
exit /b %configure_result%
