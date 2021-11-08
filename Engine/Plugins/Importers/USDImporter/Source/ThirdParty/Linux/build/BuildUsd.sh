#!/bin/bash

UE_SOURCE_DIR="./release-5.0"

UE_THIRD_PARTY_SOURCE_DIR="$UE_SOURCE_DIR/Engine/Source/ThirdParty"
UE_THIRD_PARTY_BINARIES_DIR="$UE_SOURCE_DIR/Engine/Binaries/ThirdParty"

PYTHON_SOURCE_DIR="$UE_THIRD_PARTY_SOURCE_DIR/Python3/Linux"
PYTHON_INCLUDE_DIR="$PYTHON_SOURCE_DIR/include/python3.9"
PYTHON_STATIC_LIB="$PYTHON_SOURCE_DIR/lib/libpython3.9.a"

PYTHON_BINARIES_DIR="$UE_THIRD_PARTY_BINARIES_DIR/Python3/Linux"
PYTHON_EXECUTABLE="$PYTHON_BINARIES_DIR/bin/python3"

# Run Engine/Build/BatchFiles/Linux/SetupToolchain.sh first to ensure
# that the toolchain is setup and verify that this name matches.
TOOLCHAIN_NAME=v19_clang-11.0.1-centos7

ARCH_NAME=x86_64-unknown-linux-gnu

UE_TOOLCHAIN_LOCATION="$UE_SOURCE_DIR/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$TOOLCHAIN_NAME/$ARCH_NAME"

export CC="$UE_TOOLCHAIN_LOCATION/bin/clang"
export CXX="$UE_TOOLCHAIN_LOCATION/bin/clang++"

COMPILERFLAGS="-std=c++11 -fPIC -nostdinc++ -I$UE_THIRD_PARTY_SOURCE_DIR/Unix/LibCxx/include/c++/v1 -I$PYTHON_INCLUDE_DIR"
LINKERFLAGS="-L$UE_THIRD_PARTY_SOURCE_DIR/Unix/LibCxx/lib/Linux/$ARCH_NAME/ -lc++ -lc++abi $PYTHON_STATIC_LIB -lm -lc -lgcc_s -lgcc -lutil"

export CXXFLAGS="$COMPILERFLAGS"
export LDFLAGS="-stdlib=libc++ $LINKERFLAGS"
export LIBS="$UE_THIRD_PARTY_SOURCE_DIR/Unix/LibCxx/lib/Linux/$ARCH_NAME/libc++.a $UE_THIRD_PARTY_SOURCE_DIR/Unix/LibCxx/lib/Linux/$ARCH_NAME/libc++abi.a"

CMAKE_LINKER_FLAGS="-stdlib=libc++ $LINKERFLAGS"

BUILDARGS="-DCMAKE_CXX_FLAGS='$COMPILERFLAGS' -DCMAKE_EXE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_MODULE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_SHARED_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_STATIC_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DPYTHON_INCLUDE_DIR=$PYTHON_INCLUDE_DIR -DPYTHON_LIBRARY=$PYTHON_STATIC_LIB -DPYTHON_EXECUTABLE=$PYTHON_EXECUTABLE"

BOOST_BUILDARGS="toolset=clang cxxflags='$COMPILERFLAGS' linkflags='-stdlib=libc++ $LINKERFLAGS'"

TBB_BUILDARGS="compiler=clang stdlib=libc++"

$PYTHON_EXECUTABLE src/build_scripts/build_usd.py ./Build --no-tests --no-examples --no-tutorials --no-tools --no-docs --no-imaging --build-args USD,"$BUILDARGS" Boost,"$BOOST_BUILDARGS" TBB,"$TBB_BUILDARGS"
