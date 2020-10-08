#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
# This script gets can be used to build and clean individual projects using UnrealBuildTool

set -e

cd "`dirname "$0"`/../../../.." 

# Setup Environment and Mono
if [ ${UE_USE_DOTNET:=0} -ne 0 ]; then
  source Engine/Build/BatchFiles/Linux/SetupEnvironment.sh -dotnet Engine/Build/BatchFiles/Linux
else
  source Engine/Build/BatchFiles/Linux/SetupEnvironment.sh -mono Engine/Build/BatchFiles/Linux
fi

if [ ${UE_USE_DOTNET:=0} -ne 0 ]; then
  if ! dotnet build Engine/Source/Programs/UnrealBuildTool/UnrealBuildToolCore.csproj -c Development; then
    echo "Failed to build to build tool (UnrealBuildTool)"
    exit 1
  fi
else
# First make sure that the UnrealBuildTool is up-to-date
if ! xbuild /property:Configuration=Development /verbosity:quiet /nologo /p:NoWarn=1591 Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj; then
  echo "Failed to build to build tool (UnrealBuildTool)"
  exit 1
fi

fi

if [ ${UE_USE_DOTNET:=0} -ne 0 ]; then
  echo Running command : Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"
  Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"
else
  echo Running command : Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"
  mono Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"
fi
exit $?
