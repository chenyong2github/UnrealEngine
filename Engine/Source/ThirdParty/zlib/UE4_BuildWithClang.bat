REM Copyright Epic Games, Inc. All Rights Reserved.
@echo off

SETLOCAL

SET ZLIBVERSION=zlib-1.2.11
SET ZLIBSHA256=c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1

REM This build script relies on VS2019 being present with clang support installed
CALL "%VS2019INSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"

IF NOT EXIST "%ZLIBVERSION%\src" mkdir %ZLIBVERSION%\src

REM download archive if not present
IF NOT EXIST "%ZLIBVERSION%\%ZLIBVERSION%.tar.gz" curl https://zlib.net/%ZLIBVERSION%.tar.gz --output %ZLIBVERSION%\%ZLIBVERSION%.tar.gz

REM validate checksum using Windows 10 builtin tools
certUtil -hashfile %ZLIBVERSION%\%ZLIBVERSION%.tar.gz SHA256 | findstr %ZLIBSHA256%

REM errorlevel set by findstr should be 0 when the text has been found
IF %ERRORLEVEL% NEQ 0 (
   echo Checksum failed on %ZLIBVERSION%.tar.gz
   goto :exit
)

echo %ZLIBVERSION%.tar.gz SHA256 checksum is valid

REM tar is now present by default on Windows 10
tar -xf %ZLIBVERSION%\%ZLIBVERSION%.tar.gz -C %ZLIBVERSION%\src

REM apply our patched version of CMakeLists that contains the minizip contrib files in the static library for Windows
copy /Y %ZLIBVERSION%\CMakeLists.txt %ZLIBVERSION%\src\%ZLIBVERSION%

REM prepare output folder and move to it
if not exist "%ZLIBVERSION%\lib\Win64-llvm\Release" mkdir %ZLIBVERSION%\lib\Win64-llvm\Release
pushd %ZLIBVERSION%\lib\Win64-llvm\Release

REM make sure everything is properly regenerated
if exist "CMakeCache.txt" del /Q CMakeCache.txt

REM clang and cmake must also be installed and available in your environment path
cmake -G "Ninja" -DCMAKE_C_COMPILER:FILEPATH="clang-cl.exe" -DCMAKE_C_FLAGS_RELWITHDEBINFO:STRING="/MD /Zi /O2 /Ob2 /DNDEBUG /Qvec" -DCMAKE_BUILD_TYPE="RelWithDebInfo" ..\..\..\src\%ZLIBVERSION%

REM clean and build
ninja clean
ninja

popd

if not exist "%ZLIBVERSION%\lib\Win64-llvm\Debug" mkdir %ZLIBVERSION%\lib\Win64-llvm\Debug
pushd %ZLIBVERSION%\lib\Win64-llvm\Debug

REM make sure everything is properly regenerated
if exist "CMakeCache.txt" del /Q CMakeCache.txt

REM clang and cmake must also be installed and available in your environment path
cmake -G "Ninja" -DCMAKE_C_COMPILER:FILEPATH="clang-cl.exe" -DCMAKE_C_FLAGS_DEBUG:STRING="/MDd /Zi /Ob0 /Od /RTC1" -DCMAKE_BUILD_TYPE="Debug" ..\..\..\src\%ZLIBVERSION%

REM clean and build
ninja clean
ninja

popd

:exit

ENDLOCAL
