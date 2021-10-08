@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal EnableDelayedExpansion EnableExtensions

if [%1]==[] goto usage

set BLOSC_VERSION=%1
set BLOSC_VERSION_FILENAME=v%BLOSC_VERSION%

set BLOSC_URL=https://github.com/Blosc/c-blosc/archive/%BLOSC_VERSION_FILENAME%.zip
set BLOSC_INTERMEDIATE_PATH=..\..\..\..\..\Intermediate\ThirdParty\c-blosc
set BLOSC_VERSION_INTERMEDIATE_PATH=%BLOSC_INTERMEDIATE_PATH%\c-blosc-%BLOSC_VERSION%

set PATH_SCRIPT=%~dp0
set BLOSC_INSTALL_PATH=%PATH_SCRIPT%..\..\Deploy\c-blosc-%BLOSC_VERSION%
set BLOSC_INSTALL_BIN_PATH=%BLOSC_INSTALL_PATH%\bin\Win64\VS2019
set BLOSC_INSTALL_INCLUDE_PATH=%BLOSC_INSTALL_PATH%\include
set BLOSC_INSTALL_LIB_PATH=%BLOSC_INSTALL_PATH%\lib\Win64\VS2019
set PATH_TO_THIRDPARTY=%PATH_SCRIPT%..\..\..

REM Clean install folder
if exist %BLOSC_INSTALL_PATH% (rmdir %BLOSC_INSTALL_PATH% /s/q)

REM Clean intermediate folder
if exist %BLOSC_INTERMEDIATE_PATH% (rmdir %BLOSC_INTERMEDIATE_PATH% /s/q)

mkdir %BLOSC_INTERMEDIATE_PATH%
pushd %BLOSC_INTERMEDIATE_PATH%

echo Downloading %BLOSC_URL%...
powershell -Command "(New-Object Net.WebClient).DownloadFile('%BLOSC_URL%', '%BLOSC_VERSION_FILENAME%.zip')"

echo Extracting %BLOSC_VERSION_FILENAME%.zip...
powershell -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%BLOSC_VERSION_FILENAME%.zip', '.')"

pushd c-blosc-%BLOSC_VERSION%

echo Building Blosc %BLOSC_VERSION_FILENAME%...

REM Dependency - ZLib
set PATH_TO_ZLIB=%PATH_TO_THIRDPARTY%\zlib\v1.2.8
set PATH_TO_ZLIB_SRC=%PATH_TO_ZLIB%\include\Win64\VS2015
set PATH_TO_ZLIB_LIB=%PATH_TO_ZLIB%\lib\Win64\VS2015\Release
set ZLIB_LIBRARY=%PATH_TO_ZLIB_LIB%\zlibstatic.lib

REM Temporary build directories (used as working directories when running CMake)
set BLOSC_BUILD_PATH=build_win64_VS2019

REM Build for VS2019 (64-bit)
echo Generating Blosc solution for VS2019 (64-bit)...
mkdir %BLOSC_BUILD_PATH%
pushd %BLOSC_BUILD_PATH%
cmake -G "Visual Studio 16 2019" -A x64^
    -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF ..^
    -DCMAKE_INSTALL_PREFIX=%BLOSC_INSTALL_PATH%^
    -DCMAKE_PREFIX_PATH="%PATH_TO_ZLIB_SRC%"^
    -DZLIB_LIBRARY="%ZLIB_LIBRARY%"

echo.
echo Compiling Blosc Release libraries...
cmake --build . --config Release -j8

echo.
echo Compiling Blosc Debug libraries...
cmake --build . --config Debug -j8

echo.
echo Installing headers to %BLOSC_INSTALL_INCLUDE_PATH%
mkdir %BLOSC_INSTALL_INCLUDE_PATH%
robocopy ..\blosc %BLOSC_INSTALL_INCLUDE_PATH% blosc.h /nfl /ndl /njh /njs

echo Installing Release output to %BLOSC_INSTALL_BIN_PATH%\Release and %BLOSC_INSTALL_LIB_PATH%\Release
mkdir %BLOSC_INSTALL_BIN_PATH%\Release
mkdir %BLOSC_INSTALL_LIB_PATH%\Release
robocopy blosc\Release %BLOSC_INSTALL_BIN_PATH%\Release blosc.dll /nfl /ndl /njh /njs
robocopy blosc\Release %BLOSC_INSTALL_LIB_PATH%\Release *.lib /nfl /ndl /njh /njs

echo Installing Debug output to %BLOSC_INSTALL_BIN_PATH%\Debug and %BLOSC_INSTALL_LIB_PATH%\Debug
mkdir %BLOSC_INSTALL_BIN_PATH%\Debug
mkdir %BLOSC_INSTALL_LIB_PATH%\Debug
robocopy blosc\Debug %BLOSC_INSTALL_BIN_PATH%\Debug blosc.dll /nfl /ndl /njh /njs
robocopy blosc\Debug %BLOSC_INSTALL_LIB_PATH%\Debug *.lib /nfl /ndl /njh /njs
robocopy blosc\Debug %BLOSC_INSTALL_LIB_PATH%\Debug *.pdb /nfl /ndl /njh /njs

popd
popd
popd

goto :eof

:usage
echo Usage: BuildForWindows 1.5.0
exit /B 1

endlocal
