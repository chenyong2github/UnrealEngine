@echo off
setlocal

rem ## Unreal Engine 4 AutomationTool setup script
rem ## Copyright Epic Games, Inc. All Rights Reserved.
rem ##
rem ## This script is expecting to exist in the UE4/Engine/Build/BatchFiles directory.  It will not work correctly
rem ## if you copy it to a different location and run it.

pushd "%~dp0..\..\Source"

mkdir ..\Intermediate\ProjectFiles >NUL 2>NUL

rem Look for any platform extension .cs files, and add them to a file that will be refernced by UBT project, so it can bring them in automatically
set REFERENCE_FILE="%~dp0..\..\Intermediate\ProjectFiles\UnrealBuildTool.csproj.References"
echo ^<?xml version="1.0" encoding="utf-8"?^> > %REFERENCE_FILE%
echo ^<Project ToolsVersion="15.0" DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"^> >> %REFERENCE_FILE%
echo ^<ItemGroup^> >> %REFERENCE_FILE%

if exist "..\..\Engine\Platforms" (
	pushd "..\..\Engine\Platforms"
	for /d %%i in ("*") do (
		if exist "%%i\Source\Programs\UnrealBuildTool" (
			pushd "%%i\Source\Programs\UnrealBuildTool"
			for %%j in ("*.cs") do (
				echo   ^<PlatformExtensionCompile Include="..\..\..\..\Engine\Platforms\%%i\Source\Programs\UnrealBuildTool\%%j"^> >> %REFERENCE_FILE%
				echo     ^<Link^>Platform\%%i\%%j^</Link^> >> %REFERENCE_FILE%
				echo   ^</PlatformExtensionCompile^> >> %REFERENCE_FILE%
			)
			popd
		)
	)
	popd
)
echo ^</ItemGroup^> >> %REFERENCE_FILE%
echo ^</Project^> >> %REFERENCE_FILE%