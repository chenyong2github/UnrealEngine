REM ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

set PATH_TO_CMAKE_FILE=%CD%\..\

REM Temporary build directories (used as working directories when running CMake)
set VS2017_X86_PATH="%PATH_TO_CMAKE_FILE%..\Win32\VS2015\Build"
set VS2017_X64_PATH="%PATH_TO_CMAKE_FILE%..\Win64\VS2015\Build"

REM MSBuild Directory
if "%VSWHERE%"=="" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version 15.0 -requires Microsoft.Component.MSBuild -property installationPath`) do SET _msbuild=%%i\MSBuild\15.0\Bin\MSBuild.exe
if not exist "%_msbuild%" goto MSBuildMissing

REM Build for VS2017 (32-bit)
echo Generating vivoxclientapi solution for VS2017 (32-bit)...
if exist "%VS2017_X86_PATH%" (rmdir "%VS2017_X86_PATH%" /s/q)
mkdir "%VS2017_X86_PATH%"
cd "%VS2017_X86_PATH%"
cmake -G "Visual Studio 15 2017" -DCMAKE_SUPPRESS_REGENERATION=1 -DVIVOXSDK_PATH=../../vivox-sdk/Include -DUSE_LOGIN_SESSION_AUDIO_SETTINGS=1 %PATH_TO_CMAKE_FILE%
echo Building vivoxclientapi solution for VS2017 (32-bit, Debug)...
"%_msbuild%" vivoxclientapi.sln /t:build /p:Configuration=Debug /p:Platform=Win32
echo Building vivoxclientapi solution for VS2017 (32-bit, RelWithDebInfo)...
"%_msbuild%" vivoxclientapi.sln /t:build /p:Configuration=RelWithDebInfo /p:Platform=Win32
cd ".."
if exist "Release" (rmdir "Release" /s/q)
move RelWithDebInfo Release
cd "%PATH_TO_CMAKE_FILE%"
rmdir "%VS2017_X86_PATH%" /s/q

REM Build for VS2017 (64-bit)
echo Generating vivoxclientapi solution for VS2017 (64-bit)...
if exist "%VS2017_X64_PATH%" (rmdir "%VS2017_X64_PATH%" /s/q)
mkdir "%VS2017_X64_PATH%"
cd "%VS2017_X64_PATH%"
cmake -G "Visual Studio 15 2017 Win64" -DCMAKE_SUPPRESS_REGENERATION=1 -DVIVOXSDK_PATH=../../vivox-sdk/Include -DUSE_LOGIN_SESSION_AUDIO_SETTINGS=1 %PATH_TO_CMAKE_FILE%
echo Building vivoxclientapi solution for VS2017 (64-bit, Debug)...
"%_msbuild%" vivoxclientapi.sln /t:build /p:Configuration=Debug /p:Platform=x64
echo Building vivoxclientapi solution for VS2017 (64-bit, RelWithDebInfo)...
"%_msbuild%" vivoxclientapi.sln /t:build /p:Configuration=RelWithDebInfo /p:Platform=x64
cd ".."
if exist "Release" (rmdir "Release" /s/q)
move RelWithDebInfo Release
cd "%PATH_TO_CMAKE_FILE%"
rmdir "%VS2017_X64_PATH%" /s/q
goto Exit

:MSBuildMissing
echo MSBuild not found. Please check your Visual Studio install and try again.
goto Exit

:Exit
endlocal