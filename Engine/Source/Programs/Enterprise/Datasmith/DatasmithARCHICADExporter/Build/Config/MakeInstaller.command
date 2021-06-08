#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

# Get directory of the command file
ScriptPath="`dirname "$0"`"

"$ScriptPath/MakeInstaller.sh"

open "$ScriptPath/../../../../../../../../Engine/Binaries/Mac/DatasmithArchiCADExporter"
