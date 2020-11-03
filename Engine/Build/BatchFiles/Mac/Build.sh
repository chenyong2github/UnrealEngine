#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.
# This script gets can be used to build and clean individual projects using UnrealBuildTool

cd "`dirname "$0"`/../../../.."

# Setup Environment and Mono
if [ ${UE_USE_DOTNET:=0} -ne 0 ]; then
  source Engine/Build/BatchFiles/Mac/SetupEnvironment.sh -dotnet Engine/Build/BatchFiles/Mac
else
  source Engine/Build/BatchFiles/Mac/SetupEnvironment.sh -mono Engine/Build/BatchFiles/Mac
fi

# Skip UBT and SWC compile step if we're coming in on an SSH connection (ie remote toolchain)
if [ -z "$SSH_CONNECTION" ]; then
	# First make sure that the UnrealBuildTool is up-to-date
	if [ ${UE_USE_DOTNET:=0} -ne 0 ]; then
		if ! dotnet build Engine/Source/Programs/UnrealBuildTool/UnrealBuildToolCore.csproj -c Development; then
			echo "Failed to build to build tool (UnrealBuildTool)"
			exit 1
		else
			if ! xbuild /property:Configuration=Development /verbosity:quiet /nologo /p:NoWarn=1591 Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj; then
				echo "Failed to build to build tool (UnrealBuildTool)"
				exit 1
			fi
		fi
	fi

	# build SCW if specified
	for i in "$@" ; do
		if [[ $i == "-buildscw" ]] ; then
			echo Building ShaderCompileWorker...
			mono Engine/Binaries/DotNET/UnrealBuildTool.exe ShaderCompileWorker Mac Development
			break
		fi
	done
fi

if [ ${UE_USE_DOTNET:=0} -ne 0 ]; then
  echo Running Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"
  Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"
else
  echo Running Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"
  mono Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"
fi

ExitCode=$?
if [ $ExitCode -eq 254 ] || [ $ExitCode -eq 255 ] || [ $ExitCode -eq 2 ]; then
	exit 0
else
	exit $ExitCode
fi
