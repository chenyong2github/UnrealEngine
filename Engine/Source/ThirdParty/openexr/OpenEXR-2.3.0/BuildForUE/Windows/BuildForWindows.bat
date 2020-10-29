@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

set PATH_TO_CMAKE_FILE=%CD%\..\..

set OPENEXR_LIBS=IlmImf IlmImfUtil Half Iex IexMath IlmThread Imath
set OPENEXR_VER_SUFFIX=-2_3

set PATH_TO_ZLIB=%CD%\..\..\..\..\zlib\v1.2.8
set PATH_TO_ZLIB_WIN32_SRC=%PATH_TO_ZLIB%\include\Win32\VS2015
set PATH_TO_ZLIB_WIN64_SRC=%PATH_TO_ZLIB%\include\Win64\VS2015
set PATH_TO_ZLIB_WIN32_LIB=%PATH_TO_ZLIB%\lib\Win32\VS2015\Release
set PATH_TO_ZLIB_WIN64_LIB=%PATH_TO_ZLIB%\lib\Win64\VS2015\Release

REM Temporary build directories (used as working directories when running CMake)
set OPENEXR_INTERMEDIATE_PATH=%PATH_TO_CMAKE_FILE%\..\..\..\..\Intermediate\ThirdParty\OpenEXR
set OPENEXR_INTERMEDIATE_PATH_X86=%OPENEXR_INTERMEDIATE_PATH%\win32\VS2015
set OPENEXR_INTERMEDIATE_PATH_X64=%OPENEXR_INTERMEDIATE_PATH%\win64\VS2015

set OPENEXR_INSTALL_PATH=%PATH_TO_CMAKE_FILE%\..\Deploy\OpenEXR-2.3.0\OpenEXR
set OPENEXR_INSTALL_PATH_HEADERS=%OPENEXR_INSTALL_PATH%\include
set OPENEXR_INSTALL_PATH_LIBS_X86_DEB=%OPENEXR_INSTALL_PATH%\lib\VS2015\Win32\StaticDebug
set OPENEXR_INSTALL_PATH_LIBS_X86_REL=%OPENEXR_INSTALL_PATH%\lib\VS2015\Win32\StaticRelease
set OPENEXR_INSTALL_PATH_LIBS_X64_DEB=%OPENEXR_INSTALL_PATH%\lib\VS2015\x64\StaticDebug
set OPENEXR_INSTALL_PATH_LIBS_X64_REL=%OPENEXR_INSTALL_PATH%\lib\VS2015\x64\StaticRelease

REM Build for VS2015 (64-bit)
echo Generating OpenEXR solution for VS2015 (64-bit)...
if exist %OPENEXR_INTERMEDIATE_PATH_X64% (rmdir %OPENEXR_INTERMEDIATE_PATH_X64% /s/q)
mkdir %OPENEXR_INTERMEDIATE_PATH_X64%
pushd %OPENEXR_INTERMEDIATE_PATH_X64%
cmake -G "Visual Studio 14 2015" -A x64 %PATH_TO_CMAKE_FILE%^
	-DOPENEXR_BUILD_OPENEXR=ON^
	-DOPENEXR_BUILD_PYTHON_LIBS=OFF^
	-DOPENEXR_BUILD_TESTS=OFF^
	-DOPENEXR_BUILD_SHARED=OFF^
	-DOPENEXR_BUILD_STATIC=ON^
	-DOPENEXR_NAMESPACE_VERSIONING=ON^
	-DCMAKE_PREFIX_PATH="%PATH_TO_ZLIB_WIN64_SRC%;%PATH_TO_ZLIB_WIN64_LIB%"^
	-DCMAKE_INSTALL_PREFIX="%OPENEXR_INTERMEDIATE_PATH_X64%\install"
echo Compiling OpenEXR libraries for VS2015 (64-bit)...
cmake --build . --config Debug -j8
cmake --build . --config Release -j8
cmake --install . --config Debug
cmake --install . --config Release
popd
echo Copying OpenEXR headers and libraries (64-bit)...

REM Clean destination folders
if exist %OPENEXR_INSTALL_PATH_HEADERS% (rmdir %OPENEXR_INSTALL_PATH_HEADERS% /s/q)
if exist %OPENEXR_INSTALL_PATH_LIBS_X64_DEB% (rmdir %OPENEXR_INSTALL_PATH_LIBS_X64_DEB% /s/q)
if exist %OPENEXR_INSTALL_PATH_LIBS_X64_REL% (rmdir %OPENEXR_INSTALL_PATH_LIBS_X64_REL% /s/q)

REM Install to destination
xcopy  "%OPENEXR_INTERMEDIATE_PATH_X64%\install\include" %OPENEXR_INSTALL_PATH_HEADERS% /i/y/q/r/e
FOR %%i IN (%OPENEXR_LIBS%) DO (
xcopy "%OPENEXR_INTERMEDIATE_PATH_X64%\install\lib\%%i%OPENEXR_VER_SUFFIX%_s_d.lib" "%OPENEXR_INSTALL_PATH_LIBS_X64_DEB%\%%i.lib*" /i/y/q/r
xcopy "%OPENEXR_INTERMEDIATE_PATH_X64%\install\lib\%%i%OPENEXR_VER_SUFFIX%_s.lib" "%OPENEXR_INSTALL_PATH_LIBS_X64_REL%\%%i.lib*" /i/y/q/r
)

REM Clean intermediate directory
rmdir %OPENEXR_INTERMEDIATE_PATH_X64% /s/q


REM Build for VS2015 (32-bit)
echo Generating OpenEXR solution for VS2015 (32-bit)...
if exist %OPENEXR_INTERMEDIATE_PATH_X86% (rmdir %OPENEXR_INTERMEDIATE_PATH_X86% /s/q)
mkdir %OPENEXR_INTERMEDIATE_PATH_X86%
pushd %OPENEXR_INTERMEDIATE_PATH_X86%
cmake -G "Visual Studio 14 2015" -A Win32 %PATH_TO_CMAKE_FILE%^
	-DOPENEXR_BUILD_OPENEXR=ON^
	-DOPENEXR_BUILD_PYTHON_LIBS=OFF^
	-DOPENEXR_BUILD_TESTS=OFF^
	-DOPENEXR_BUILD_SHARED=OFF^
	-DOPENEXR_BUILD_STATIC=ON^
	-DOPENEXR_NAMESPACE_VERSIONING=ON^
	-DCMAKE_PREFIX_PATH="%PATH_TO_ZLIB_WIN32_SRC%;%PATH_TO_ZLIB_WIN32_LIB%"^
	-DCMAKE_INSTALL_PREFIX="%OPENEXR_INTERMEDIATE_PATH_X86%\install"
echo Compiling OpenEXR libraries for VS2015 (32-bit)...
cmake --build . --config Debug -j8
cmake --build . --config Release -j8
cmake --install . --config Debug
cmake --install . --config Release
popd
echo Copying OpenEXR headers and libraries (32-bit)...

REM Clean destination folders
if exist %OPENEXR_INSTALL_PATH_HEADERS% (rmdir %OPENEXR_INSTALL_PATH_HEADERS% /s/q)
if exist %OPENEXR_INSTALL_PATH_LIBS_X86_DEB% (rmdir %OPENEXR_INSTALL_PATH_LIBS_X86_DEB% /s/q)
if exist %OPENEXR_INSTALL_PATH_LIBS_X86_REL% (rmdir %OPENEXR_INSTALL_PATH_LIBS_X86_REL% /s/q)

REM Install to destination
xcopy  "%OPENEXR_INTERMEDIATE_PATH_X86%\install\include" %OPENEXR_INSTALL_PATH_HEADERS% /i/y/q/r/e
FOR %%i IN (%OPENEXR_LIBS%) DO (
xcopy "%OPENEXR_INTERMEDIATE_PATH_X86%\install\lib\%%i%OPENEXR_VER_SUFFIX%_s_d.lib" "%OPENEXR_INSTALL_PATH_LIBS_X86_DEB%\%%i.lib*" /i/y/q/r
xcopy "%OPENEXR_INTERMEDIATE_PATH_X86%\install\lib\%%i%OPENEXR_VER_SUFFIX%_s.lib" "%OPENEXR_INSTALL_PATH_LIBS_X86_REL%\%%i.lib*" /i/y/q/r
)

REM Clean intermediate directory
rmdir %OPENEXR_INTERMEDIATE_PATH_X86% /s/q

endlocal
