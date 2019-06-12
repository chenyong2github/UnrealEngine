#!/bin/sh

PATH="/Applications/CMake.app/Contents/bin":"$PATH"

CUR_DIR="`dirname "$0"`"

cd $CUR_DIR

if [ ! -d ninja ];
	then
		git clone git://github.com/ninja-build/ninja.git && cd ninja
		git checkout release

		./configure.py --bootstrap

		cd ..
fi

PATH="$CUR_DIR/ninja":"$PATH"
export PATH="$CUR_DIR/ninja":"$PATH"

cd ShaderConductor

# p4 edit $THIRD_PARTY_CHANGELIST lib/Mac/...

# compile Mac
python BuildAll.py ninja clang x64 RelWithDebInfo

cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libdxcompiler.dylib ../../../../Binaries/Mac/libdxcompiler.3.7.dylib
cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libdxcompiler.dylib ../../../../Binaries/Mac/libdxcompiler.dylib
cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libShaderConductor.dylib ../../../../Binaries/Mac/libShaderConductor.dylib

dsymutil ../../../../Binaries/Mac/libdxcompiler.dylib
dsymutil ../../../../Binaries/Mac/libdxcompiler.3.7.dylib
dsymutil ../../../../Binaries/Mac/libShaderConductor.dylib
