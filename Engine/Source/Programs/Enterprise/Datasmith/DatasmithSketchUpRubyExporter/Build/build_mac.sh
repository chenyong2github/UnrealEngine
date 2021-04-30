#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.

set -e
set -x

if [ -z ${UE_SDKS_ROOT+x} ]; then
    echo "UE_SDKS_ROOT is unset";
    exit;
fi

PluginSrcPath=`dirname "$0"`/..
EnginePath=$PluginSrcPath/../../../../..
dylibLibFreeImage=libfreeimage-3.18.0.dylib

# Copy and fixup dylibs
Dylibs="$PluginSrcPath/.build/Dylibs"
rm -rf "$Dylibs"
mkdir -p "$Dylibs"
cp "$EnginePath/Binaries/ThirdParty/FreeImage/Mac/$dylibLibFreeImage" $Dylibs
cp "$EnginePath/Binaries/Mac/DatasmithSDK/DatasmithSDK.dylib" $Dylibs
install_name_tool -id @loader_path/Dylibs/DatasmithSDK.dylib "$Dylibs/DatasmithSDK.dylib"
install_name_tool -id @loader_path/Dylibs/$dylibLibFreeImage "$Dylibs/$dylibLibFreeImage"
install_name_tool -change @rpath/$dylibLibFreeImage @loader_path/$dylibLibFreeImage "$Dylibs/DatasmithSDK.dylib"

BuildSketchUpPlugin() {
    SUVERSION=${1}
    SUSDKVERSION=${2}

    # Compile plugin bundle
    BuildDir="$PluginSrcPath/.build/$SUVERSION"
    rm -rf "$BuildDir"
    mkdir -p "$BuildDir"
    #DEBUG_BUILD_SCRIPT="--no-compile --verbose"
    DEBUG_BUILD_SCRIPT="--verbose"
    python3 build_mac.py --multithread --sdks-root="$UE_SDKS_ROOT" --sketchup-version=$SUVERSION --sketchup-sdk-version=$SUSDKVERSION --output-path="$BuildDir" --datasmithsdk-lib="$Dylibs/DatasmithSDK.dylib" $DEBUG_BUILD_SCRIPT

    # Copy plugin files as they should b ein the plugin folder:
    # ruby code
    cp -r "$PluginSrcPath/Plugin" "$BuildDir"
    # support libs
    cp -r "$Dylibs" "$BuildDir/Plugin/UnrealDatasmithSketchUp"
    # plugin bundle
    cp "$BuildDir/bin/DatasmithSketchUpRuby.bundle" "$BuildDir/Plugin/UnrealDatasmithSketchUp"
}

BuildSketchUpPlugin 2019 SDK_Mac_2019-3-252
BuildSketchUpPlugin 2020 SDK_Mac_2020-2-171
BuildSketchUpPlugin 2021 SDK_Mac_2021-0-338

# install_name_tool -change @rpath/DatasmithSDK.dylib @loader_path/Dylibs/DatasmithSDK.dylib DatasmithSketchUpRuby.bundle 
# install_name_tool -change @loader_path/libfreeimage-3.18.0.dylib @loader_path/Dylibs/libfreeimage-3.18.0.dylib DatasmithSketchUpRuby.bundle 
