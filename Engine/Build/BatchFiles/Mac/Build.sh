#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.
# This script gets can be used to build and clean individual projects using UnrealBuildTool

cd "`dirname "$0"`/../../../.."

# Setup Environment for DotNET
source Engine/Build/BatchFiles/Mac/SetupEnvironment.sh -dotnet Engine/Build/BatchFiles/Mac

# Skip UBT and SWC compile step if this is an installed build.
if [ ! -f Engine/Build/InstalledBuild.txt ]; then
    # First make sure that the UnrealBuildTool is up-to-date
    if ! dotnet build Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj -c Development -v quiet; then
	  echo "Failed to build to build tool (UnrealBuildTool)"
	  exit 1
    fi

    # build SCW if specified
    for i in "$@" ; do
	if [[ $i == "-buildscw" ]] ; then
		echo Building ShaderCompileWorker...
		Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool ShaderCompileWorker Mac Development
		break
	fi
    done
fi

echo Running Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"
Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool "$@"

ExitCode=$?
if [ $ExitCode -eq 254 ] || [ $ExitCode -eq 255 ] || [ $ExitCode -eq 2 ]; then
	exit 0
else
	exit $ExitCode
fi
