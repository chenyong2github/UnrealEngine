#!/bin/bash

SOURCE_LOCATION=`cd $(pwd)/..; pwd`
BUILD_LOCATION="$(pwd)/build_mac"

rm -rf $BUILD_LOCATION
mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION


echo Configuring Release build
cmake -G "Unix Makefiles" $SOURCE_LOCATION -DISA_SSE41=ON -DISA_NEON=ON -DCLI=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15

echo Building for Release...
cmake --build ./ --target astcenc-static

LIB_LOCATION="$SOURCE_LOCATION/lib/Mac/Release"

echo Copying astcenc to $LIB_LOCATION
cp -f $BUILD_LOCATION/Source/libastcenc-static.a $LIB_LOCATION/libastcenc-static.a

popd

rm -rf $BUILD_LOCATION

echo Done.