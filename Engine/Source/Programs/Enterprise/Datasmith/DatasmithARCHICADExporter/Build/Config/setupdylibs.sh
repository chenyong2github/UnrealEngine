#!/bin/sh

set -e

ConfigPath=`dirname "$0"`
projectPath=$ConfigPath/..

RelativeEnginePath=$projectPath/../../../../../../../Engine
EnginePath=`python -c "import os; print(os.path.realpath('$RelativeEnginePath'))"`

echo "UE_SDKS_ROOT = ${UE_SDKS_ROOT}"

if [ -z ${UE_SDKS_ROOT+x} ]; then
    echo "UE_SDKS_ROOT is unset";
    exit -1;
fi

if [ ! -f "$ConfigPath/SDKsRoot.xcconfig" ]; then
    echo "Create $ConfigPath/SDKsRoot.xcconfig"
    echo UESDKRoot = $UE_SDKS_ROOT > "$ConfigPath/SDKsRoot.xcconfig"
    echo UE_Engine = $EnginePath >> "$ConfigPath/SDKsRoot.xcconfig"
fi

# Remove ArchiCAD resource tool from quarantine
pushd "$UE_SDKS_ROOT/HostMac/Mac/Archicad"
	chmod 777 23/Support/Tools/OSX/ResConv
	xattr -r -d com.apple.quarantine 23/Support/Tools/OSX/ResConv

	chmod 777 24/Support/Tools/OSX/ResConv
	xattr -r -d com.apple.quarantine 24/Support/Tools/OSX/ResConv

	chmod 777 25/Support/Tools/OSX/ResConv
	xattr -r -d com.apple.quarantine 25/Support/Tools/OSX/ResConv
popd

OurDylibFolder=$projectPath/Dylibs

mkdir -p "$OurDylibFolder"

dylibLibFreeImage=libfreeimage-3.18.0.dylib

if [[ "$EnginePath/Binaries/ThirdParty/FreeImage/Mac/$dylibLibFreeImage" -nt "$OurDylibFolder/$dylibLibFreeImage" ]]; then
	if [ -f "$OurDylibFolder/$dylibLibFreeImage" ]; then
		unlink "$OurDylibFolder/$dylibLibFreeImage"
	fi
fi
if [ ! -f "$OurDylibFolder/$dylibLibFreeImage" ]; then
    echo "Copy $dylibLibFreeImage"
    cp "$EnginePath/Binaries/ThirdParty/FreeImage/Mac/$dylibLibFreeImage" "$OurDylibFolder"
	chmod +w "$OurDylibFolder/$dylibLibFreeImage"
    install_name_tool -id @loader_path/$dylibLibFreeImage "$OurDylibFolder/$dylibLibFreeImage"
fi

SetUpDll() {
	DylibName=$1
	OriginalDylibPath="$EnginePath/Binaries/Mac/DatasmithUE4ArchiCAD/$DylibName"

	if [[ "$OriginalDylibPath" -nt "$OurDylibFolder/$DylibName" ]]; then
		if [ -f "$OurDylibFolder/$DylibName" ]; then
			unlink "$OurDylibFolder/$DylibName"
		fi
	fi
	if [ ! -f "$OurDylibFolder/$DylibName" ]; then
		if [ -f "$OriginalDylibPath" ]; then
			echo "Copy $DylibName"
			cp "$OriginalDylibPath" "$OurDylibFolder"
			install_name_tool -id @loader_path/$DylibName "$OurDylibFolder/$DylibName"
			install_name_tool -change @rpath/$dylibLibFreeImage @loader_path/$dylibLibFreeImage "$OurDylibFolder/$DylibName"
		else
			echo "Missing $DylibName"
		fi
	fi
}

SetUpDll DatasmithUE4ArchiCAD.dylib
SetUpDll DatasmithUE4ArchiCAD-Mac-Debug.dylib
