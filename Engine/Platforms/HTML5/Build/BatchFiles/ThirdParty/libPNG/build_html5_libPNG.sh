#!/bin/bash
set -x -e

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Platforms/HTML5/Build/BatchFiles/Build_All_HTML5_libs.sh


ZLIB_VERSION='zlib-1.2.5'
ZLIB_HTML5_SRC="$UE4_TPS_SRC/zlib/$ZLIB_VERSION/Src"
ZLIB_HTML5_DST="$HTML5_TPS_LIBS/zlib/$ZLIB_VERSION"
ZLIB_HTML5_BUILDS="$(pwd)/../zlib/"

PNG_VERSION='libPNG-1.5.2'
PNG_HTML5_SRC="$UE4_TPS_SRC/libPNG/$PNG_VERSION"
PNG_HTML5_DST="$HTML5_TPS_LIBS/libPNG/$PNG_VERSION"


# local destination
if [ ! -d "$PNG_HTML5_DST" ]; then
	mkdir -p "$PNG_HTML5_DST"
fi
# TODO change this to p4 checkout
if [ ! -z "$(ls -A "$PNG_HTML5_DST")" ]; then
	chmod +w "$PNG_HTML5_DST"/*
fi


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
		d=d
	else
		DBGFLAG=NDEBUG
		d=
	fi
	EMFLAGS="$UE_EMFLAGS"
	# ----------------------------------------
	emcmake cmake -G "Unix Makefiles" \
		-DM_LIBRARY="" \
		-DZLIB_INCLUDE_DIR="$ZLIB_HTML5_SRC" \
		-DZLIB_LIBRARY="$ZLIB_HTML5_DST" \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS -I\"$ZLIB_HTML5_SRC\" -I\"$ZLIB_HTML5_BUILDS/BUILD$SUFFIX\" " \
		"$PNG_HTML5_SRC"
	cmake --build . -- png15_static VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	cp libpng15${d}.$UE_LIB_EXT "$PNG_HTML5_DST"/libpng${SUFFIX}.$UE_LIB_EXT
	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l "$PNG_HTML5_DST"

