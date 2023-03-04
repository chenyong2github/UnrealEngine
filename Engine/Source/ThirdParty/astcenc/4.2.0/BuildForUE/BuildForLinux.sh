#!/bin/bash

set -e

ARCH_NAME=x86_64-unknown-linux-gnu

UE_ENGINE_LOCATION=`cd $(pwd)/../../../..; pwd`

UE_THIRD_PARTY_LOCATION=`cd $(pwd)/../..; pwd`

SOURCE_LOCATION=`cd $(pwd)/..; pwd`

BUILD_LOCATION="$(pwd)/build_linux"

rm -rf $BUILD_LOCATION
mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION

# Run Engine/Build/BatchFiles/Linux/SetupToolchain.sh first to ensure
# that the toolchain is setup and verify that this name matches.
TOOLCHAIN_NAME=v20_clang-13.0.1-centos7

UE_TOOLCHAIN_LOCATION="$UE_ENGINE_LOCATION/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$TOOLCHAIN_NAME/$ARCH_NAME"

C_COMPILER="$UE_TOOLCHAIN_LOCATION/bin/clang"
CXX_COMPILER="$UE_TOOLCHAIN_LOCATION/bin/clang++"

CXX_FLAGS="-std=c++11 -stdlib=libc++ -pthread -fPIC -I$UE_THIRD_PARTY_LOCATION/Unix/LibCxx/include/c++/v1"
CXX_LINKER="-L$UE_THIRD_PARTY_LOCATION/Unix/LibCxx/lib/Unix/$ARCH_NAME/ -lc++"
CMAKE_ARGS=(
	-DCMAKE_POLICY_DEFAULT_CMP0056=NEW
	-DCMAKE_C_COMPILER="$C_COMPILER" 
	-DCMAKE_CXX_COMPILER="$CXX_COMPILER" 
	-DCMAKE_CXX_FLAGS="$CXX_FLAGS" 
	-DCMAKE_EXE_LINKER_FLAGS="$CXX_LINKER" 
	-DCMAKE_MODULE_LINKER_FLAGS="$CXX_LINKER" 
	-DCMAKE_SHARED_LINKER_FLAGS="$CXX_LINKER")

echo Configuring Release build
cmake -G "Unix Makefiles" $SOURCE_LOCATION -DISA_SSE41=ON -DCLI=OFF -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARGS[@]}"

echo Building for Release...
cmake --build ./ --target astcenc-sse4.1-static

LIB_LOCATION="$SOURCE_LOCATION/lib/Linux/Release"

echo Copying astcenc to $LIB_LOCATION
cp -f $BUILD_LOCATION/Source/libastcenc-sse4.1-static.a $LIB_LOCATION/libastcenc-sse4.1-static.a

popd

rm -rf $BUILD_LOCATION

echo Done.
