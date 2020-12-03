#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

EnvironmentType=-dotnet

# If ran from somewhere other then the script location we'll have the full base path
BASE_PATH="`dirname "$0"`"

if [ ! -d "$BASE_PATH/../../Binaries/DotNET" ]; then
 echo RunUBT ERROR: It looks like you're missing some files that are required in order to run UBT.  Please check that you've downloaded and unpacked the engine source code, binaries, content and third-party dependencies before running this script.
 exit 1
fi

if [ ! -d "$BASE_PATH/../../Source" ]; then
 echo RunUBT ERROR: This script file does not appear to be located inside the Engine/Build/BatchFiles directory.
 exit 1
fi

if [ "$(uname)" = "Darwin" ]; then
	# Setup Environment
	source "$BASE_PATH/Mac/SetupEnvironment.sh" $EnvironmentType "$BASE_PATH/Mac"
fi

if [ "$(uname)" = "Linux" ]; then
	# Setup Environment
	source "$BASE_PATH/Linux/SetupEnvironment.sh" $EnvironmentType "$BASE_PATH/Linux"
fi

if [ -f "$BASE_PATH/../../Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" ]; then 
  dotnet msbuild /restore /target:build /property:Configuration=Development /nologo $BASE_PATH/../../Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj /verbosity:quiet

  if [ $? -ne 0 ]; then
    echo RunUBT ERROR: Failed to build UnrealBuildTool
    exit 1
  fi
fi

# pass all parameters to UBT
"$BASE_PATH/../../Binaries/DotNET/UnrealBuildTool/UnrealBuildTool" "$@"
