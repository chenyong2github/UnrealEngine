#!/bin/bash
## Copyright Epic Games, Inc. All Rights Reserved.
##
## Unreal Engine 4 AutomationTool setup script
##
## This script is expecting to exist in the UE4/Engine/Build/BatchFiles directory.  It will not work
## correctly if you copy it to a different location and run it.


SCRIPT_DIR=$(cd "`dirname "$0"`" && pwd)
cd "$SCRIPT_DIR/../../Source"

mkdir -p ../Intermediate/ProjectFiles

## Look for any platform extension .cs files, and add them to a file that will be refernced by UBT project, so it can bring them in automatically
REFERENCE_FILE=../Intermediate/ProjectFiles/UnrealBuildTool.csproj.References
echo "<?xml version=\"1.0\" encoding=\"utf-8\"?>" > $REFERENCE_FILE
echo "<Project ToolsVersion=\"15.0\" DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">" >> $REFERENCE_FILE
echo "<ItemGroup>" >> $REFERENCE_FILE

for PLATFORM in ../../Engine/Platforms/*; do
	for SOURCE_FILE in ${PLATFORM}/Source/Programs/UnrealBuildTool/*; do
		echo "<PlatformExtensionCompile Include=\"..\\..\\${SOURCE_FILE//\//\\}\">" >> $REFERENCE_FILE
		echo "    <Link>Platform\\${PLATFORM##*/}\\${SOURCE_FILE##*/}</Link>" >> $REFERENCE_FILE
		echo "</PlatformExtensionCompile>" >> $REFERENCE_FILE
	done
done

echo "</ItemGroup>" >> $REFERENCE_FILE
echo "</Project>" >> $REFERENCE_FILE

