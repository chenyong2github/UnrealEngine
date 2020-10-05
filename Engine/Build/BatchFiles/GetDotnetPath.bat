@echo off
setlocal

rem ## Unreal Engine 5 utility script
rem ## Copyright Epic Games, Inc. All Rights Reserved.
rem ##
rem ## This script verifies that dotnet sdk is installed and a new enough SDK is present.

for /f "delims=" %%i in ('where dotnet') do (
	REM Dotnet exists
	goto find_sdks
)

REM Dotnet command did not exist
exit /B 1

:find_sdks

set REQUIRED_MAJOR_VERSION=3
set REQUIRED_MINOR_VERSION=1

set FOUND_MAJOR=
set FOUND_MINOR=
REM Unfortunately dotnet lists the sdks in oldest version first, thus we will pick the oldest version that matches our criteria as valid.
REM This does not really matter as we are just trying to verify that a new enough SDK is actually present 
for /f "tokens=1,* delims= " %%I in ('dotnet --list-sdks') do (

	for /f "tokens=1,2,3 delims=." %%X in ("%%I") do (
		REM We can check the patch version for preview versions and ignore those, but it slowed down this batch to much so accepting those for now. 
		REM We do not actually use the determined version for anything so usually the newest SDK installed is used anyway
		if %%X EQU %REQUIRED_MAJOR_VERSION% (
			REM If the major version is the same as we require we check the minor version
			if %%Y GEQ %REQUIRED_MINOR_VERSION% (

				set FOUND_MAJOR=%%X
				set FOUND_MINOR=%%Y
				goto Succeeded
			)
		)

		if %%X GTR %REQUIRED_MAJOR_VERSION% (
			REM If the major version is greater then what we require then this sdk is good enough
			set FOUND_MAJOR=%%X
			set FOUND_MINOR=%%Y
			goto Succeeded
		)

	)

)

REM Dotnet is installed but the sdk present is to old
exit /B 1

:Succeeded
ECHO Found Dotnet SDK version: %FOUND_MAJOR%.%FOUND_MINOR%
exit /B 0
	