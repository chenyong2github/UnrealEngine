#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.

set -e
set -x

if [ -z ${UE_SDKS_ROOT+x} ]; then
    echo "UE_SDKS_ROOT is unset";
    exit -1;
fi

pushd `dirname "$0"`/..
PluginSrcPath=`pwd`
cd "$PluginSrcPath/../../../../.."
EnginePath=`pwd`
popd

mkdir -p /tmp/Python3/Deploy
ln -f -s "$EnginePath/Binaries/ThirdParty/Python3/Mac/lib" /tmp/Python3/Deploy

IntermediatePath="$EnginePath/Intermediate/Build/Mac/x86_64/SketchUp"
rm -rf "$IntermediatePath"
mkdir -p "$IntermediatePath"

dylibLibFreeImage=libfreeimage-3.18.0.dylib

# Copy and fixup dylibs
Dylibs="$IntermediatePath/Dylibs"
rm -rf "$Dylibs"
mkdir -p "$Dylibs"

cp "$EnginePath/Binaries/ThirdParty/FreeImage/Mac/$dylibLibFreeImage" "$Dylibs"
cp "$EnginePath/Binaries/Mac/DatasmithSDK/DatasmithSDK.dylib" "$Dylibs"

chmod 777 "$Dylibs/$dylibLibFreeImage"

install_name_tool -id @loader_path/Dylibs/DatasmithSDK.dylib "$Dylibs/DatasmithSDK.dylib"
install_name_tool -id @loader_path/Dylibs/$dylibLibFreeImage "$Dylibs/$dylibLibFreeImage"
install_name_tool -change @rpath/$dylibLibFreeImage @loader_path/$dylibLibFreeImage "$Dylibs/DatasmithSDK.dylib"

BuildSketchUpPlugin() {
    SUVERSION=${1}
    SUSDKVERSION=${2}

    # Compile plugin bundle
    # BuildDir="$PluginSrcPath/.build/$SUVERSION"
    BuildDir="$EnginePath/Binaries/Mac/SketchUp/$SUVERSION"
    IntermediateDir="$IntermediatePath/$SUVERSION"
    rm -rf "$BuildDir"
    mkdir -p "$BuildDir"
    #DEBUG_BUILD_SCRIPT="--no-compile --verbose"
    #DEBUG_BUILD_SCRIPT="--verbose"
    DEBUG_BUILD_SCRIPT=
    "$EnginePath/Binaries/ThirdParty/Python3/Mac/bin/python3.7" "$PluginSrcPath/Build/build_mac.py" --multithread --sdks-root="$UE_SDKS_ROOT" --sketchup-version=$SUVERSION --sketchup-sdk-version=$SUSDKVERSION --output-path="$BuildDir" --intermediate-path="$IntermediateDir" --datasmithsdk-lib="$Dylibs/DatasmithSDK.dylib" $DEBUG_BUILD_SCRIPT

    # Copy plugin files as they should b ein the plugin folder:
    # ruby code
    cp -r "$PluginSrcPath/Plugin" "$BuildDir"
    # support libs
    cp -r "$Dylibs" "$BuildDir/Plugin/UnrealDatasmithSketchUp"
    # resources
    cp -r "$PluginSrcPath/Resources/Windows" "$BuildDir/Plugin/UnrealDatasmithSketchUp"
    mv "$BuildDir/Plugin/UnrealDatasmithSketchUp/Windows" "$BuildDir/Plugin/UnrealDatasmithSketchUp/Resources"

    #version file
    "$EnginePath/Binaries/ThirdParty/Python3/Mac/bin/python3.7" "$PluginSrcPath/Build/create_version_file.py" "$EnginePath" "$BuildDir/Plugin/UnrealDatasmithSketchUp/version"
}

BuildSketchUpPlugin 2019 SDK_Mac_2019-3-252
BuildSketchUpPlugin 2020 SDK_Mac_2020-2-171
BuildSketchUpPlugin 2021 SDK_Mac_2021-0-338

# install_name_tool -change @rpath/DatasmithSDK.dylib @loader_path/Dylibs/DatasmithSDK.dylib DatasmithSketchUp.bundle 
# install_name_tool -change @loader_path/libfreeimage-3.18.0.dylib @loader_path/Dylibs/libfreeimage-3.18.0.dylib DatasmithSketchUp.bundle

rm -f "$EnginePath/Intermediate/Build/Mac/x86_64/SketchUp/Packages"
mkdir -p "$EnginePath/Intermediate/Build/Mac/x86_64/SketchUp/Packages"

rm -rf /tmp/Python3

