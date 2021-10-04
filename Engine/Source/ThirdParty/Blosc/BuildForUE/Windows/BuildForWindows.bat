@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

if [%1]==[] goto usage

set BLOSC_VERSION=%1
set BLOSC_VERSION_FILENAME=v%BLOSC_VERSION%

set BLOSC_URL=https://github.com/Blosc/c-blosc/archive/%BLOSC_VERSION_FILENAME%.zip
set BLOSC_INTERMEDIATE_PATH=..\..\..\..\..\Intermediate\ThirdParty\c-blosc
set BLOSC_VERSION_INTERMEDIATE_PATH=%BLOSC_INTERMEDIATE_PATH%\c-blosc-%BLOSC_VERSION%

set BLOSC_INSTALL_PATH_VS2019_X64=%CD%\..\..\c-blosc-%BLOSC_VERSION%\Win64\VS2019

if exist %BLOSC_INTERMEDIATE_PATH% (rmdir %BLOSC_INTERMEDIATE_PATH% /s/q)

mkdir %BLOSC_INTERMEDIATE_PATH%
pushd %BLOSC_INTERMEDIATE_PATH%

echo Downloading %BLOSC_URL%...
powershell -Command "(New-Object Net.WebClient).DownloadFile('%BLOSC_URL%', '%BLOSC_VERSION_FILENAME%.zip')"

echo Extracting %BLOSC_VERSION_FILENAME%.zip...
powershell -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%BLOSC_VERSION_FILENAME%.zip', '.')"

pushd c-blosc-%BLOSC_VERSION%

echo Building Blosc %BLOSC_VERSION_FILENAME%...

REM Temporary build directories (used as working directories when running CMake)
set BLOSC_INTERMEDIATE_PATH_VS2019_X64=build_win64

REM Build for VS2019 (64-bit)
echo Generating Blosc solution for VS2019 (64-bit)...
mkdir %BLOSC_INTERMEDIATE_PATH_VS2019_X64%
pushd %BLOSC_INTERMEDIATE_PATH_VS2019_X64%
cmake -G "Visual Studio 16 2019" -A x64 -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF .. -DCMAKE_INSTALL_PREFIX=%BLOSC_INSTALL_PATH_VS2019_X64%
echo Compiling Blosc libraries for VS2019 (64-bit)...
cmake --build . --config Release -j8
rmdir %BLOSC_INSTALL_PATH_VS2019_X64% /s/q
cmake --install .
popd

popd
popd
rmdir %BLOSC_INTERMEDIATE_PATH% /s/q

goto :eof

:usage
echo Usage: BuildForWindows 1.5.0
exit /B 1

endlocal
