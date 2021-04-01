#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

# Get directory of the command file
ScriptPath="`dirname "$0"`"

# Ask user for forcing 10.12 (Minimum Archicad 23 requirement)
ForceMacOSMin=`osascript -e 'button returned of (display dialog "Force MinMacOSVersion to 10.12 ?" buttons {"Yes", "No"})'`

if [ "$ForceMacOSMin" = "Yes" ]
then
	MacOSMinVersion="10.12"
fi

"$ScriptPath/MakeInstaller.sh" "" "$MacOSMinVersion"

open "$ScriptPath/../../../../../../../../Engine/Binaries/Mac/DatasmithArchiCADExporter"
