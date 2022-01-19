#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
# This script gets can be used to build and clean individual projects using UnrealBuildTool

set -e

cd "`dirname "$0"`/../../../.." 

# Setup Environment and Mono
source Engine/Build/BatchFiles/Linux/SetupEnvironment.sh -dotnet Engine/Build/BatchFiles/Linux

# If this is a source drop of the engine make sure that the UnrealBuildTool is up-to-date
if [ ! -f Engine/Build/InstalledBuild.txt ]; then
	if ! dotnet build Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj -c Development -v quiet; then
		echo "Failed to build the build tool (UnrealBuildTool)"
		exit 1
	fi
fi

echo Running command : dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll "$@"
dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll "$@"
exit $?
