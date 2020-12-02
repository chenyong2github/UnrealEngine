#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
# This script gets can be used to build and clean individual projects using UnrealBuildTool

set -e

cd "`dirname "$0"`/../../../.." 

# Setup Environment and Mono
source Engine/Build/BatchFiles/Linux/SetupEnvironment.sh -dotnet Engine/Build/BatchFiles/Linux

if ! dotnet build Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj -c Development -v quiet; then
  echo "Failed to build to build tool (UnrealBuildTool)"
  exit 1
fi

echo Running command : Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"
Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"
exit $?
