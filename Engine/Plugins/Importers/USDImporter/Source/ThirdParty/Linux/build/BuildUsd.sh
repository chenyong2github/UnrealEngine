#!/bin/bash

UE_THIRD_PARTY_DIR="../../../../../../Source/ThirdParty"

export CC=clang
export CXX=clang++

export CXXFLAGS="-std=c++11 -fPIC -nostdinc++ -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include/c++/v1"
CMAKE_LINKER_FLAGS="-stdlib=libc++ -L$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/ $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a"

BUILDARGS="-DCMAKE_EXE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_MODULE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_SHARED_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_STATIC_LINKER_FLAGS='$CMAKE_LINKER_FLAGS'"

BOOST_BUILDARGS="cxxflags='-stdlib=libc++ -fPIC -I$PYTHON_INSTALL_DIR/include/python2.7 -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include/c++/v1' toolset=clang linkflags='-stdlib=libc++ -L$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a'"

python USD-19.01/build_scripts/build_usd.py ./Build --no-tests --no-docs --no-imaging --build-args USD,"$BUILDARGS", Boost,"$BOOST_BUILDARGS"



