#!/bin/bash
set -x -e

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Platforms/HTML5/Build/BatchFiles/Build_All_HTML5_libs.sh


SDL2_VERSION='SDL-gui-backend'
SDL2_HTML5_SRC="$UE4_TPS_SRC/SDL2/$SDL2_VERSION"
SDL2_HTML5_DST="$HTML5_TPS_LIBS/SDL2/$SDL2_VERSION"


# local destination
if [ ! -d "$SDL2_HTML5_DST" ]; then
	mkdir -p "$SDL2_HTML5_DST"
fi
# TODO change this to p4 checkout
if [ ! -z "$(ls -A "$SDL2_HTML5_DST")" ]; then
	chmod +w "$SDL2_HTML5_DST"/*
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
		DSUFFIX="d"
	else
		DBGFLAG=NDEBUG
		DSUFFIX=""
	fi
	EMFLAGS="$UE_EMFLAGS"
	# ----------------------------------------
	emcmake cmake -G "Unix Makefiles" \
		-DBUILD_SHARED_LIBS=FALSE \
		-DLIBTYPE=STATIC \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS" \
		"$SDL2_HTML5_SRC"
	cmake --build . -- -j VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	cp libSDL2${DSUFFIX}.$UE_LIB_EXT "$SDL2_HTML5_DST"/libSDL2${SUFFIX}.$UE_LIB_EXT
	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l "$SDL2_HTML5_DST"

