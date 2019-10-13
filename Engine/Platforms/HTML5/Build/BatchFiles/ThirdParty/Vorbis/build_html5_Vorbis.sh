#!/bin/bash
set -x -e

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Platforms/HTML5/Build/BatchFiles/Build_All_HTML5_libs.sh


OGG_VERSION='libogg-1.2.2'
OGG_HTML5_SRC="$UE4_TPS_SRC/Ogg/$OGG_VERSION"
OGG_HTML5_DST="$HTML5_TPS_LIBS/Ogg/$OGG_VERSION"

VORBIS_VERSION='libvorbis-1.3.2'
VORBIS_HTML5_SRC="$UE4_TPS_SRC/Vorbis/$VORBIS_VERSION"
VORBIS_HTML5_DST="$HTML5_TPS_LIBS/Vorbis/$VORBIS_VERSION"
VORBIS_FLAGS="-I\"$OGG_HTML5_SRC/include\" -Wno-comment -Wno-shift-op-parentheses"


# local destination
if [ ! -d "$VORBIS_HTML5_DST" ]; then
	mkdir -p "$VORBIS_HTML5_DST"
fi
# TODO change this to p4 checkout
if [ ! -z "$(ls -A "$VORBIS_HTML5_DST")" ]; then
	chmod +w "$VORBIS_HTML5_DST"/*
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
	else
		DBGFLAG=NDEBUG
	fi
	EMFLAGS="$UE_EMFLAGS"
	# ----------------------------------------
	emcmake cmake -G "Unix Makefiles" \
		-DBUILD_SHARED_LIBS=OFF \
		-DOGG_INCLUDE_DIRS=$OGG_HTML5_SRC/include \
		-DOGG_LIBRARIES=$OGG_HTML5_DST \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS $VORBIS_FLAGS" \
		-DCMAKE_CXX_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS $VORBIS_FLAGS" \
		"$VORBIS_HTML5_SRC"
	cmake --build . -- -j VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	cp lib/libvorbis.$UE_LIB_EXT "$VORBIS_HTML5_DST"/libvorbis${SUFFIX}.$UE_LIB_EXT
#	cp lib/libvorbisenc.$UE_LIB_EXT "$VORBIS_HTML5_DST"/libvorbisenc${SUFFIX}.$UE_LIB_EXT
	cp lib/libvorbisfile.$UE_LIB_EXT "$VORBIS_HTML5_DST"/libvorbisfile${SUFFIX}.$UE_LIB_EXT
	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l "$VORBIS_HTML5_DST"

