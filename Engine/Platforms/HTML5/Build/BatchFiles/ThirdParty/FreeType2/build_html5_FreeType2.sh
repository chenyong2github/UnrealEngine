#!/bin/bash
set -x -e

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Platforms/HTML5/Build/BatchFiles/Build_All_HTML5_libs.sh


FT2_HTML5=$(pwd)
FT2_VERSION='FreeType2-2.10.0'
FT2_HTML5_SRC="$UE4_TPS_SRC/FreeType2/$FT2_VERSION"
FT2_HTML5_DST="$HTML5_TPS_LIBS/FreeType2/$FT2_VERSION"

# FT2 dependencies
HARFBUZZ_VERSION='harfbuzz-2.4.0'
HARFBUZZ_HTML5_SRC="$UE4_TPS_SRC/HarfBuzz/$HARFBUZZ_VERSION/src"
HARFBUZZ_HTML5_LIB="$HTML5_TPS_LIBS/HarfBuzz/$HARFBUZZ_VERSION/lib"
PNG_VERSION='libPNG-1.5.27'
PNG_HTML5_SRC="$UE4_TPS_SRC/libPNG/$PNG_VERSION"
PNG_HTML5_LIB="$HTML5_TPS_LIBS/libPNG/$PNG_VERSION/lib"
ZLIB_VERSION='v1.2.8'
ZLIB_HTML5_INC="$HTML5_TPS_LIBS/zlib/$ZLIB_VERSION/include"
ZLIB_HTML5_LIB="$HTML5_TPS_LIBS/zlib/$ZLIB_VERSION/lib"


# local destination
if [ ! -d "$FT2_HTML5_DST/lib" ]; then
	mkdir -p "$FT2_HTML5_DST/lib"
fi
# TODO remove this p4 hack after HTML5 becomes community driven only
if [ ! -z "$(ls -A "$FT2_HTML5_DST/lib")" ]; then
	chmod +w "$FT2_HTML5_DST"/lib/*
fi


# ----------------------------------------
if [ ! -d "$FT2_HTML5_SRC/docs" ]; then
	# needed as of FreeType2-2.10.0 (from ue4's fork)
	mkdir -p "$FT2_HTML5_SRC"/docs
	touch "$FT2_HTML5_SRC"/docs/LICENSE.TXT
fi

# ----------------------------------------
# WASM does not support SIMD instructions yet
if [ ! -e "$FT2_HTML5_SRC/src/sfnt/pngshim.c.save" ]; then
	mv "$FT2_HTML5_SRC/src/sfnt/pngshim.c" "$FT2_HTML5_SRC/src/sfnt/pngshim.c.save"
fi
REPLACE_CODE='    !defined( __EMSCRIPTEN__ )                                         && \\
'

cat "$FT2_HTML5_SRC/src/sfnt/pngshim.c.save" | perl -0p -e "s/(.*__ORDER_LITTLE_ENDIAN.*)/$REPLACE_CODE\1/" > "$FT2_HTML5_SRC/src/sfnt/pngshim.c"
# ----------------------------------------


build_via_cmake()
{
	SUFFIX=_O$OLEVEL
	OPTIMIZATION=-O$OLEVEL
	# ----------------------------------------
	rm -rf BUILD$SUFFIX
	mkdir BUILD$SUFFIX
	cd BUILD$SUFFIX
	# ----------------------------------------
#	TYPE=${type^^} # OSX-bash doesn't like this
	TYPE=`echo $type | tr "[:lower:]" "[:upper:]"`
	if [ $TYPE == "DEBUG" ]; then
		DBGFLAG=_DEBUG
	else
		DBGFLAG=NDEBUG
	fi
	EMFLAGS="$UE_EMFLAGS"
	# ----------------------------------------
	emcmake cmake -G "Unix Makefiles" \
		-DBUILD_SHARED_LIBS=OFF \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS" \
		-DPNG_PNG_INCLUDE_DIR="$PNG_HTML5_SRC" \
		-DPNG_LIBRARY="$PNG_HTML5_LIB" \
		-DHARFBUZZ_INCLUDE_DIRS="$HARFBUZZ_HTML5_SRC" \
		-DHARFBUZZ_LIBRARIES="$HARFBUZZ_HTML5_LIB" \
		-DZLIB_INCLUDE_DIR="$ZLIB_HTML5_INC" \
		-DZLIB_LIBRARY_DEBUG="$ZLIB_HTML5_LIB/zlib_Oz.$UE_LIB_EXT" \
		-DZLIB_LIBRARY_RELEASE="$ZLIB_HTML5_LIB/zlib_Oz.$UE_LIB_EXT" \
		"$FT2_HTML5_SRC"
	cmake --build . -- freetype -j VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	cp libfreetype*.$UE_LIB_EXT "$FT2_HTML5_DST"/lib/libfreetype${SUFFIX}.$UE_LIB_EXT
	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l "$FT2_HTML5_DST/lib"


# NOTE: for CIS system that error out on warnings, may need to add in CMAKE_C_FLAGS above: -Wno-deprecated-declarations


