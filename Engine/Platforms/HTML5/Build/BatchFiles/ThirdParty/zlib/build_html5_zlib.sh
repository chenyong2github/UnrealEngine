#!/bin/bash
set -x -e

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Platforms/HTML5/Build/BatchFiles/Build_All_HTML5_libs.sh

ZLIB_HTML5=$(pwd)
ZLIB_VERSION='v1.2.8'

# building from ZLIB tar file archive
ZLIB_TAR_SRC="$UE4_TPS_SRC/zlib/$ZLIB_VERSION/build"
ZLIB_TAR_NAME='zlib-1.2.8'

ZLIB_HTML5_SRC="$ZLIB_HTML5/$ZLIB_TAR_NAME"
ZLIB_HTML5_DST="$HTML5_TPS_LIBS/zlib/$ZLIB_VERSION"


# local destination
if [ ! -d "$ZLIB_HTML5_DST/include" ]; then
	mkdir -p "$ZLIB_HTML5_DST/include"
fi
if [ ! -d "$ZLIB_HTML5_DST/lib" ]; then
	mkdir -p "$ZLIB_HTML5_DST/lib"
fi
# TODO remove this p4 hack after HTML5 becomes community driven only
if [ ! -z "$(ls -A "$ZLIB_HTML5_DST"/lib)" ]; then
	chmod +w "$ZLIB_HTML5_DST"/lib/*
fi


# ----------------------------------------
# building from ZLIB tar file archive
if [ ! -d $ZLIB_TAR_NAME ]; then
	tar xf "$ZLIB_TAR_SRC"/$ZLIB_TAR_NAME.tar.gz
	cp $ZLIB_TAR_NAME/zlib.h $ZLIB_HTML5_DST/include/.
fi
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
	rm -f "$ZLIB_HTML5_SRC"/zconf.h
	emcmake cmake -G "Unix Makefiles" \
		-DBUILD_SHARED_LIBS=OFF \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS -Wno-shift-negative-value" \
		"$ZLIB_HTML5_SRC"
	cmake --build . -- zlib -j VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	cp libz.$UE_LIB_EXT "$ZLIB_HTML5_DST"/lib/zlib${SUFFIX}.$UE_LIB_EXT

	# note: zconf.h is the same from all optimization builds...
	cp zconf.h "$ZLIB_HTML5_DST"/include/.

	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l "$ZLIB_HTML5_DST/lib"

