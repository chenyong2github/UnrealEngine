#!/bin/sh

projectPath=`dirname "$0"`/..

relativeEnginePath=$projectPath/../../../../../../Engine
enginePath=`python -c "import os; print(os.path.realpath('$relativeEnginePath'))"`

if [ -z ${UE_SDKS_ROOT+x} ]; then
    echo "UE_SDKS_ROOT is unset";
    exit;
fi

if [ ! -f "$projectPath/SDKsRoot.xcconfig" ]; then
    echo "Create SDKsRoot.xcconfig"
    echo UESDKRoot = $UE_SDKS_ROOT > "$projectPath/Config/SDKsRoot.xcconfig"
    echo UE_Engine = $enginePath >> "$projectPath/Config/SDKsRoot.xcconfig"
fi

dylibsDir=$projectPath/Dylibs

mkdir -p "$dylibsDir"

dylibLibFreeImage=libfreeimage-3.18.0.dylib

if [[ "$enginePath/Binaries/ThirdParty/FreeImage/Mac/$dylibLibFreeImage" -nt "$dylibsDir/$dylibLibFreeImage" ]]; then
  unlink "$dylibsDir/$dylibLibFreeImage"
fi
if [ ! -f "$dylibsDir/$dylibLibFreeImage" ]; then
    echo "Copy $dylibLibFreeImage"
    cp "$enginePath/Binaries/ThirdParty/FreeImage/Mac/$dylibLibFreeImage" "$dylibsDir"
    install_name_tool -id @loader_path/$dylibLibFreeImage "$dylibsDir/$dylibLibFreeImage"
fi

SetUpDll() {
	dylibDatasmithSDK=$1

	if [[ "$enginePath/Binaries/Mac/DatasmithSDK/$dylibDatasmithSDK" -nt "$dylibsDir/$dylibDatasmithSDK" ]]; then
	  unlink "$dylibsDir/$dylibDatasmithSDK"
	fi
	if [ ! -f "$dylibsDir/$dylibDatasmithSDK" ]; then
		echo "Copy $dylibDatasmithSDK"
		cp "$enginePath/Binaries/Mac/DatasmithSDK/$dylibDatasmithSDK" "$dylibsDir"

		install_name_tool -id @loader_path/$dylibDatasmithSDK "$dylibsDir/$dylibDatasmithSDK"
		install_name_tool -change @rpath/$dylibLibFreeImage @loader_path/$dylibLibFreeImage "$dylibsDir/$dylibDatasmithSDK"
	fi
}

SetUpDll DatasmithSDK.dylib
SetUpDll DatasmithSDK-Mac-Debug.dylib
