#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.
# This script gets can be used to build and clean individual projects using UnrealBuildTool

cd "`dirname "$0"`/../../../.."

# Setup Environment and Mono
source Engine/Build/BatchFiles/Mac/SetupEnvironment.sh -mono Engine/Build/BatchFiles/Mac


# Skip UBT and SWC compile step if we're coming in on an SSH connection (ie remote toolchain)
# or if this is an installed build
if [ -z "$SSH_CONNECTION" ] && [ ! -f Engine/Build/InstalledBuild.txt ]; then
	# First make sure that the UnrealBuildTool is up-to-date
	if ! xbuild /property:Configuration=Development /verbosity:quiet /nologo /p:NoWarn=1591 Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj; then
		echo "Failed to build to build tool (UnrealBuildTool)"
		exit 1
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

echo Running Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"
mono Engine/Binaries/DotNET/UnrealBuildTool.exe "$@"

ExitCode=$?
if [ $ExitCode -eq 254 ] || [ $ExitCode -eq 255 ] || [ $ExitCode -eq 2 ]; then
	exit 0
else
	exit $ExitCode
fi
