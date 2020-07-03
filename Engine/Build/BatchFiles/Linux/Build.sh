#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
# This script gets can be used to build and clean individual projects using UnrealBuildTool

set -e

cd "`dirname "$0"`/../../../.." 

# Setup Environment and Mono
source Engine/Build/BatchFiles/Linux/SetupEnvironment.sh -mono Engine/Build/BatchFiles/Linux

# First make sure that the UnrealBuildTool is up-to-date
if ! xbuild /property:Configuration=Development /verbosity:quiet /nologo /p:NoWarn=1591 Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj; then
  echo "Failed to build to build tool (UnrealBuildTool)"
  exit 1
fi

echo Running command : Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"
mono Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"
exit $?
