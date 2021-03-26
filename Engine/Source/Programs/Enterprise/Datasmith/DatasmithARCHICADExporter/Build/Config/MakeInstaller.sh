#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

set -e

# Get directory of the command file
ScriptPath="`dirname "$0"`"

UE4RootPath=$ScriptPath/../../../../../../../..
UE4RootPath=`python -c "import os; print(os.path.realpath('$UE4RootPath'))"`

echo "Build DatasmithSDK"
xcodebuild -workspace "$UE4RootPath/UE4.xcworkspace" -scheme DatasmithSDK -configuration Development build -quiet

"$ScriptPath/setupdylibs.sh"

echo "Clean DatasmithARCHICAD23Exporter"
xcodebuild -project "$ScriptPath/../DatasmithARCHICADExporter.xcodeproj" -scheme DatasmithARCHICAD23Exporter -configuration Release clean -quiet

echo "Build DatasmithARCHICAD23Exporter"
xcodebuild -project "$ScriptPath/../DatasmithARCHICADExporter.xcodeproj" -scheme DatasmithARCHICAD23Exporter -configuration Release build -quiet

"$ScriptPath/../../Installer/MacOS/SignAndBuild.sh" "$1"

echo "Done"
