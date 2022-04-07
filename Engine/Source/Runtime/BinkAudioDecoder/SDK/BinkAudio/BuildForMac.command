#!/bin/bash

set -e
UE_THIRD_PARTY_LOCATION=`cd $(pwd)/..; pwd`

UE_MODULE_LOCATION=`pwd`

SOURCE_LOCATION="$UE_MODULE_LOCATION"
BUILD_LOCATION="$UE_MODULE_LOCATION/Intermediate"

# Specify all of the include/bin/lib directory variables so that CMake can
# compute relative paths correctly for the imported targets.
INSTALL_LOCATION="$UE_MODULE_LOCATION/Lib/"

rm -rf $BUILD_LOCATION
mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_LOCATION"
    -DCMAKE_OSX_DEPLOYMENT_TARGET="10.9"
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
)

echo Configuring build for BinkAudio...
cmake -G "Xcode" $SOURCE_LOCATION "${CMAKE_ARGS[@]}"

echo Building BinkAudio for Debug...
cmake --build . --config Debug

echo Installing BinkAudio for Debug...
cmake --install . --config Debug

echo Building BinkAudio for Release...
cmake --build . --config Release

echo Installing BinkAudio for Release...
cmake --install . --config Release

popd

cp "$BUILD_LOCATION/Release/liblibbinka_ue_encode.a" "$INSTALL_LOCATION/libbinka_ue_encode_osx_static.a"
cp "$BUILD_LOCATION/Release/liblibbinka_ue_decode.a" "$INSTALL_LOCATION/libbinka_ue_decode_osx_static.a"

echo Done.
