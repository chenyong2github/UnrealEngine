#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

echo
echo Setting up Unreal Engine 5 project files...
echo

# If ran from somewhere other then the script location we'll have the full base path
BASE_PATH="`dirname "$0"`"

# this is located inside an extra 'Mac' path unlike the Windows variant.

if [ ! -d "$BASE_PATH/../../../Binaries/DotNET" ]; then
 echo GenerateProjectFiles ERROR: It looks like you're missing some files that are required in order to generate projects.  Please check that you've downloaded and unpacked the engine source code, binaries, content and third-party dependencies before running this script.
 exit 1
fi

if [ ! -d "$BASE_PATH/../../../Source" ]; then
 echo GenerateProjectFiles ERROR: This script file does not appear to be located inside the Engine/Build/BatchFiles/Mac directory.
 exit 1
fi

source "$BASE_PATH/SetupEnvironment.sh" -dotnet "$BASE_PATH"

if [ -f "$BASE_PATH/../../../Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" ]; then
  dotnet build $BASE_PATH/../../../Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj -c Development -v quiet

  if [ $? -ne 0 ]; then
    echo GenerateProjectFiles ERROR: Failed to build UnrealBuildTool
    exit 1
  fi
fi

# pass all parameters to UBT
"$BASE_PATH/../../../Binaries/DotNET/UnrealBuildTool/UnrealBuildTool" -projectfiles "$@"
