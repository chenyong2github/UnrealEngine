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
		dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll ShaderCompileWorker Mac Development
		break
	fi
    done
fi

echo Running dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll "$@"
dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll "$@"

ExitCode=$?
if [ $ExitCode -eq 254 ] || [ $ExitCode -eq 255 ] || [ $ExitCode -eq 2 ]; then
	exit 0
else
	exit $ExitCode
fi
