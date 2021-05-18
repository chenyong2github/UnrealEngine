#!/bin/bash

UE_SOURCE_DIR="./release-4.27"

UE_THIRD_PARTY_DIR="$UE_SOURCE_DIR/Engine/Source/ThirdParty"
PYTHON_INSTALL_DIR="$UE_SOURCE_DIR/Engine/Source/ThirdParty/Python3/Linux" 
PYTHON_EXECUTABLE_DIR="$UE_SOURCE_DIR/Engine/Binaries/ThirdParty/Python3/Linux"
PYTHON_INCLUDE_DIR="$UE_SOURCE_DIR/Engine/Source/ThirdParty/Python3/Linux" 

COMPILERFLAGS="-std=c++11 -fPIC -nostdinc++ -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include/c++/v1 -I$PYTHON_INCLUDE_DIR/include/python3.7 -I$PYTHON_INCLUDE_DIR/include/x86_64-unknown-linux-gnu" 
LINKERFLAGS="-L$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/ -lc++ -lc++abi $PYTHON_INSTALL_DIR/lib/libpython3.7m.a -lm -lc -lgcc_s -lgcc -lutil"

export CC=clang
export CXX=clang++

export CXXFLAGS="$COMPILERFLAGS"
export LDFLAGS="-stdlib=libc++ $LINKERFLAGS"
export LIBS="$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a"

CMAKE_LINKER_FLAGS="-stdlib=libc++ $LINKERFLAGS"

BUILDARGS="-DCMAKE_CXX_FLAGS='$COMPILERFLAGS' -DCMAKE_EXE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_MODULE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_SHARED_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_STATIC_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DPYTHON_INCLUDE_DIR=$PYTHON_INCLUDE_DIR/include/python3.7m -DPYTHON_LIBRARY=$PYTHON_INSTALL_DIR/lib/libpython3.7m.a -DPYTHON_EXECUTABLE=$PYTHON_EXECUTABLE_DIR/bin/python3.7"

BOOST_BUILDARGS="toolset=clang cxxflags='$COMPILERFLAGS' linkflags='-stdlib=libc++ $LINKERFLAGS'"

TBB_BUILDARGS="compiler=clang stdlib=libc++"

python3 src/build_scripts/build_usd.py ./Build --no-docs --no-tests --no-imaging --no-examples --no-tutorials --no-tools --build-args USD,"$BUILDARGS" Boost,"$BOOST_BUILDARGS" TBB,"$TBB_BUILDARGS"






