#!/usr/bin/env bash
set -e  # exit immediately on error
set -x  # print commands

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# --------------------------------------------------------------------------------
# remember to source your emscripten's env settings before using this script:
#   e.g.> source .../emsdk_clone/emsdk_env.sh
# a.k.a.> . .../emsdk_clone/emsdk_set_env.sh


# --------------------------------------------------------------------------------
# experimental

LLVMBACKEND=0 # backend to use =>  0:WASM  1:LLVM


# --------------------------------------------------------------------------------
# this flag is used in all build scripts below

#export UE_EMFLAGS="-s SIMD=1 -s USE_PTHREADS=1"
#export UE_EMFLAGS="-s SIMD=0 -s USE_PTHREADS=1"
#export UE_EMFLAGS="-s SIMD=0 -s USE_PTHREADS=1 -s WASM=1 -s BINARYEN=1" # WASM still does not play nice with SIMD
#export UE_EMFLAGS="          -s USE_PTHREADS=1 -s WASM=1 -s BINARYEN=1" # WASM still does not play nice with SIMD

if [ $LLVMBACKEND == 1 ]; then
	export UE_USE_BITECODE='OFF'
	export UE_LIB_EXT='a'
	export UE_EMFLAGS='-s WASM=1 -s WASM_OBJECT_FILES=1'
else
	export UE_USE_BITECODE='ON'
	export UE_LIB_EXT='bc'
	export UE_EMFLAGS='-s WASM=1'
fi

# --------------------------------------------------------------------------------
# helpers for new "platforms" path

export UE4_TPS_SRC="$(pwd)/../../../../Source/ThirdParty"
export HTML5_TPS_LIBS="$(pwd)/../../Source/ThirdParty"
TPS_HTML5_SCRIPTS="$(pwd)/ThirdParty"

# --------------------------------------------------------------------------------

# build all ThirdParty libs for HTML5
# from the simplest to build to the most complex and dependancy order

cd "$TPS_HTML5_SCRIPTS"/zlib;      ./build_html5_zlib.sh
cd "$TPS_HTML5_SCRIPTS"/libPNG;    ./build_html5_libPNG.sh
cd "$TPS_HTML5_SCRIPTS"/FreeType2; ./build_html5_FreeType2.sh
cd "$TPS_HTML5_SCRIPTS"/Ogg;       ./build_html5_Ogg.sh
cd "$TPS_HTML5_SCRIPTS"/Vorbis;    ./build_html5_Vorbis.sh

# WARNING: building ICU might take a while...
cd "$TPS_HTML5_SCRIPTS"/ICU;       ./build_html5_ICU.sh
cd "$TPS_HTML5_SCRIPTS"/HarfBuzz;  ./build_html5_HarfBuzz.sh

# WARNING: building PhysX might take a while...
cd "$TPS_HTML5_SCRIPTS"/PhysX3;    ./build_html5_PhysX3.sh

# WARNING: building SDL2 might take a while...
cd "$TPS_HTML5_SCRIPTS"/SDL2;      ./build_html5_SDL2.sh

# NEW!
cd "$TPS_HTML5_SCRIPTS"/libOpus;      ./build_html5_libOpus.sh


cd "$TPS_HTML5_SCRIPTS"
echo 'Success!'
#play -q /usr/share/sounds/sound-icons/glass-water-1.wav


