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

if [ "$#" -eq 1 ] && [ "$1" == "-debug" ]; then
	# Debug
	python BuildAll.py ninja clang x64 Debug

	cp -f Build/ninja-osx-clang-x64-Debug/Lib/libdxcompiler.dylib ../../../../Binaries/Mac/libdxcompiler.3.7.dylib
	cp -f Build/ninja-osx-clang-x64-Debug/Lib/libdxcompiler.dylib ../../../../Binaries/Mac/libdxcompiler.dylib
	cp -f Build/ninja-osx-clang-x64-Debug/Lib/libShaderConductor.dylib ../../../../Binaries/Mac/libShaderConductor.dylib
else
	# Release
	python BuildAll.py ninja clang x64 RelWithDebInfo

	cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libdxcompiler.dylib ../../../../Binaries/Mac/libdxcompiler.3.7.dylib
	cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libdxcompiler.dylib ../../../../Binaries/Mac/libdxcompiler.dylib
	cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libShaderConductor.dylib ../../../../Binaries/Mac/libShaderConductor.dylib
fi

dsymutil ../../../../Binaries/Mac/libdxcompiler.dylib
dsymutil ../../../../Binaries/Mac/libdxcompiler.3.7.dylib
dsymutil ../../../../Binaries/Mac/libShaderConductor.dylib
