@echo off

set CONFIG=RelWithDebInfo

REM Run build script with "-debug" argument to get debuggable shader conductor
if "%1"=="-debug" set CONFIG=Debug


if exist ShaderConductor\lib\Win64 goto Continue
echo 
echo ************************************
echo *** Creating ShaderConductor\lib\Win64...
mkdir ShaderConductor\lib
mkdir ShaderConductor\lib\Win64

:Continue
set ENGINE_THIRD_PARTY_BIN=..\..\Binaries\ThirdParty\ShaderConductor\Win64
set ENGINE_THIRD_PARTY_SOURCE=..\..\Source\ThirdParty\ShaderConductor

set VS16_ROOT_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\2019
set MSBUILD_VS16_PROFESSIONAL=%VS16_ROOT_DIR%\Professional\MSBuild\Current\Bin
set MSBUILD_VS16_ENTERPRISE=%VS16_ROOT_DIR%\Enterprise\MSBuild\Current\Bin

echo 
echo ************************************
echo *** Checking out files...
pushd ..\%ENGINE_THIRD_PARTY_BIN%
	p4 edit ..\%THIRD_PARTY_CHANGELIST% ./...
popd

pushd ShaderConductor\lib\Win64
	p4 edit ..\%THIRD_PARTY_CHANGELIST% ./...
popd

mkdir ..\..\..\Intermediate\ShaderConductor
pushd ..\..\..\Intermediate\ShaderConductor
	echo 
	echo ************************************
	echo *** CMake
	cmake -G "Visual Studio 16" -T host=x64 -A x64 %ENGINE_THIRD_PARTY_SOURCE%\ShaderConductor

	echo 
	echo ************************************
	echo *** MSBuild
	
	where MSBuild.exe >nul 2>nul
	if %ERRORLEVEL% equ 0 (
		echo Run MSBuild from environment variable
		MSbuild.exe ALL_BUILD.vcxproj -nologo -v:m -maxCpuCount -p:Platform=x64;Configuration="%CONFIG%"
	) else (
		if exist "%MSBUILD_VS16_PROFESSIONAL%\MSBuild.exe" (
			echo Run MSBuild from "%MSBUILD_VS16_PROFESSIONAL%\MSBuild.exe"
			"%MSBUILD_VS16_PROFESSIONAL%\MSBuild.exe" ALL_BUILD.vcxproj -nologo -v:m -maxCpuCount -p:Platform=x64;Configuration="%CONFIG%"
		) else (
			echo Run MSBuild from "%MSBUILD_VS16_ENTERPRISE%\MSBuild.exe"
			"%MSBUILD_VS16_ENTERPRISE%\MSBuild.exe" ALL_BUILD.vcxproj -nologo -v:m -maxCpuCount -p:Platform=x64;Configuration="%CONFIG%"
		)
	)
	
	
	echo 
	echo ************************************
	echo *** Copying to final destination
 	xcopy External\DirectXShaderCompiler\%CONFIG%\bin\dxcompiler.pdb	%ENGINE_THIRD_PARTY_BIN%\dxcompiler.pdb  /F /Y
 	xcopy Bin\%CONFIG%\dxcompiler.dll       %ENGINE_THIRD_PARTY_BIN%\dxcompiler.dll  /F /Y
 	xcopy Bin\%CONFIG%\ShaderConductor.dll  %ENGINE_THIRD_PARTY_BIN%\ShaderConductor.dll  /F /Y
 	xcopy Bin\%CONFIG%\ShaderConductor.pdb  %ENGINE_THIRD_PARTY_BIN%\ShaderConductor.pdb  /F /Y
	xcopy Lib\%CONFIG%\ShaderConductor.lib  %ENGINE_THIRD_PARTY_SOURCE%\ShaderConductor\lib\Win64 /F /Y
popd

:Done
