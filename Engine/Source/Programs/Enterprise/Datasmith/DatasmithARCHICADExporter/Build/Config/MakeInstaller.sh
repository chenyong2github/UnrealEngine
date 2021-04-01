#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

set -e

# Get directory of the command file
ScriptPath="`dirname "$0"`"

UE4RootPath=$ScriptPath/../../../../../../../..
UE4RootPath=`python -c "import os; print(os.path.realpath('$UE4RootPath'))"`

echo "Build DatasmithUE4ArchiCAD"
MacToolChain="$UE4RootPath/Engine/Source/Programs/UnrealBuildTool/Platform/Mac/MacToolChain.cs"
if [ "$2" = "10.12" ]; then
	# To build a dylib that follow Archicad 23 deployement (10.12) modify MacOSVersion and MinMacOSVersion in
	# "Engine/Source/Programs/UnrealBuildTool/Platform/Mac/MacToolChain.cs" to "10.12" and "10.12.0"
	if [ ! -f "${MacToolChain}.bak" ]; then
		echo "Force MinMacOSVersion to 10.12.0"
		mv "${MacToolChain}" "${MacToolChain}.bak"
		sed 's/10\.14.3/10.12.0/g' "${MacToolChain}.bak" | sed 's/10\.14/10.12/g' > ${MacToolChain}
		"$UE4RootPath/GenerateProjectFiles.command"
	fi
	xcodebuild -workspace "$UE4RootPath/UE4.xcworkspace" -scheme DatasmithUE4ArchiCAD -configuration Development clean -quiet
fi
xcodebuild -workspace "$UE4RootPath/UE4.xcworkspace" -scheme DatasmithUE4ArchiCAD -configuration Development build -quiet
"$ScriptPath/setupdylibs.sh"
if [ -f "${MacToolChain}.bak" ]; then
	echo "Restore standard MinMacOSVersion"
	rm "${MacToolChain}"
	mv "${MacToolChain}.bak" "${MacToolChain}"
	touch "${MacToolChain}"
	"$UE4RootPath/GenerateProjectFiles.command"
	xcodebuild -workspace "$UE4RootPath/UE4.xcworkspace" -scheme DatasmithUE4ArchiCAD -configuration Development clean -quiet
fi

echo "Build DatasmithARCHICADExporter"
xcodebuild -project "$ScriptPath/../DatasmithARCHICADExporter.xcodeproj" -scheme BuildAllAddOns -configuration Release build -quiet

DeveloperId=$1
 if [ "$DeveloperId" = "" ]; then
	# Use user environment variable to get signing id
	DeveloperId=$UE_DatasmithArchicadDeveloperId
fi

"$ScriptPath/../../Installer/MacOS/SignAndBuild.sh" "$DeveloperId"

echo "Done"
