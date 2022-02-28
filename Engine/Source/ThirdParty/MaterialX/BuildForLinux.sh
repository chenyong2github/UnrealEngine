#!/bin/bash

set -e

MATERIALX_VERSION=1.38.1

ARCH_NAME=x86_64-unknown-linux-gnu

UE_ENGINE_LOCATION=`cd $(pwd)/../../..; pwd`

UE_THIRD_PARTY_LOCATION=`cd $(pwd)/..; pwd`

UE_MODULE_LOCATION=`pwd`

SOURCE_LOCATION="$UE_MODULE_LOCATION/MaterialX-$MATERIALX_VERSION"

BUILD_LOCATION="$UE_MODULE_LOCATION/Intermediate"

INSTALL_INCLUDEDIR=include
INSTALL_LIB_DIR="Unix/$ARCH_NAME/lib"

INSTALL_LOCATION="$UE_MODULE_LOCATION/Deploy/MaterialX-$MATERIALX_VERSION"
INSTALL_INCLUDE_LOCATION="$INSTALL_LOCATION/$INSTALL_INCLUDEDIR"
INSTALL_LINUX_LOCATION="$INSTALL_LOCATION/Unix"

rm -rf $BUILD_LOCATION
rm -rf $INSTALL_INCLUDE_LOCATION
rm -rf $INSTALL_LINUX_LOCATION

mkdir $BUILD_LOCATION
pushd $BUILD_LOCATION

# Run Engine/Build/BatchFiles/Linux/SetupToolchain.sh first to ensure
# that the toolchain is setup and verify that this name matches.
TOOLCHAIN_NAME=v19_clang-11.0.1-centos7

UE_TOOLCHAIN_LOCATION="$UE_ENGINE_LOCATION/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$TOOLCHAIN_NAME/$ARCH_NAME"

C_COMPILER="$UE_TOOLCHAIN_LOCATION/bin/clang"
CXX_COMPILER="$UE_TOOLCHAIN_LOCATION/bin/clang++"

CXX_FLAGS="-fPIC -I$UE_THIRD_PARTY_LOCATION/Unix/LibCxx/include/c++/v1"
CXX_LINKER="-nodefaultlibs -L$UE_THIRD_PARTY_LOCATION/Unix/LibCxx/lib/Unix/$ARCH_NAME/ $UE_THIRD_PARTY_LOCATION/Unix/LibCxx/lib/Linux/$ARCH_NAME/libc++.a $UE_THIRD_PARTY_LOCATION/Unix/LibCxx/lib/Linux/$ARCH_NAME/libc++abi.a -lm -lc -lgcc_s -lgcc"

# Note that since we no longer support OpenGL on Linux, we cannot build the
# MaterialXRender libraries, since MaterialX does not offer a way to disable
# only MaterialXRenderGlsl, which requires linking against OpenGL.
CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_LOCATION"
    -DMATERIALX_INSTALL_INCLUDE_PATH="$INSTALL_INCLUDEDIR"
    -DMATERIALX_INSTALL_LIB_PATH="$INSTALL_LIB_DIR"
    -DCMAKE_C_COMPILER="$C_COMPILER"
    -DCMAKE_CXX_COMPILER="$CXX_COMPILER"
    -DCMAKE_CXX_FLAGS="$CXX_FLAGS"
    -DCMAKE_EXE_LINKER_FLAGS="$CXX_LINKER"
    -DCMAKE_MODULE_LINKER_FLAGS="$CXX_LINKER"
    -DCMAKE_SHARED_LINKER_FLAGS="$CXX_LINKER"
    -DMATERIALX_BUILD_RENDER=OFF
    -DMATERIALX_BUILD_TESTS=OFF
    -DMATERIALX_TEST_RENDER=OFF
    -DCMAKE_DEBUG_POSTFIX=_d
)

NUM_CPU=`grep -c ^processor /proc/cpuinfo`

echo Configuring Debug build for MaterialX version $MATERIALX_VERSION...
cmake -G "Unix Makefiles" $SOURCE_LOCATION -DCMAKE_BUILD_TYPE=Debug "${CMAKE_ARGS[@]}"

echo Building MaterialX for Debug...
cmake --build . -j$NUM_CPU

echo Installing MaterialX for Debug...
cmake --install .

# The Unix Makefiles generator does not support multiple configurations, so we
# need to re-configure for Release.
echo Configuring Release build for MaterialX version $MATERIALX_VERSION...
cmake -G "Unix Makefiles" $SOURCE_LOCATION -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARGS[@]}"

echo Building MaterialX for Release...
cmake --build . -j$NUM_CPU

echo Installing MaterialX for Release...
cmake --install .

popd

echo Done.
