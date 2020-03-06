#!/bin/bash

UE_THIRD_PARTY_DIR="/Engine/Source/ThirdParty"
PYTHON_INSTALL_DIR="/Engine/Source/ThirdParty/Python/Build/Linux/usr/local"

export CC=clang
export CXX=clang++

export CXXFLAGS="-std=c++11 -fPIC -nostdinc++ -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include/c++/v1"
export LDFLAGS="-stdlib=libc++ -L$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/ $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a"
export LIBS="$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a"

CMAKE_LINKER_FLAGS="-stdlib=libc++ -L$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/ $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a"

BUILDARGS="-DCMAKE_EXE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_MODULE_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_SHARED_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DCMAKE_STATIC_LINKER_FLAGS='$CMAKE_LINKER_FLAGS' -DPYTHON_INCLUDE_DIR=$PYTHON_INSTALL_DIR/include/python2.7 -DPYTHON_LIBRARY=$PYTHON_INSTALL_DIR/lib/libpython2.7.so -DPYTHON_EXECUTABLE=$PYTHON_INSTALL_DIR/bin/python2.7"

BOOST_BUILDARGS="toolset=clang cxxflags='-stdlib=libc++ -fPIC -I$PYTHON_INSTALL_DIR/include/python2.7 -I$PYTHON_INSTALL_DIR/include/x86_64-unknown-linux-gnu -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include -I$UE_THIRD_PARTY_DIR/Linux/LibCxx/include/c++/v1' linkflags='-stdlib=libc++ -L$UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a $UE_THIRD_PARTY_DIR/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a'"

TBB_BUILDARGS="compiler=clang stdlib=libc++"

python src/build_scripts/build_usd.py ./Build --no-tests --no-docs --no-imaging --build-args USD,"$BUILDARGS" Boost,"$BOOST_BUILDARGS" TBB,"$TBB_BUILDARGS"






