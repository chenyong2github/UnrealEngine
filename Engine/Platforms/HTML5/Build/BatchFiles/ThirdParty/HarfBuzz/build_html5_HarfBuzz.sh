#!/bin/bash
set -x -e

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Platforms/HTML5/Build/BatchFiles/Build_All_HTML5_libs.sh


HARFBUZZ_HTML5=$(pwd)
HARFBUZZ_VERSION='harfbuzz-2.4.0'
HARFBUZZ_HTML5_SRC="$UE4_TPS_SRC/HarfBuzz/$HARFBUZZ_VERSION/BuildForUE"
HARFBUZZ_HTML5_DST="$HTML5_TPS_LIBS/HarfBuzz/$HARFBUZZ_VERSION"


# local destination
if [ ! -d "$HARFBUZZ_HTML5_DST/lib" ]; then
	mkdir -p "$HARFBUZZ_HTML5_DST/lib"
fi
# TODO remove this p4 hack after HTML5 becomes community driven only
if [ ! -z "$(ls -A "$HARFBUZZ_HTML5_DST/lib")" ]; then
	chmod +w "$HARFBUZZ_HTML5_DST"/lib/*
fi


# ----------------------------------------
# CMakeLists.txt modifications -- need to nuke some more "test" code
if [ ! -e $HARFBUZZ_HTML5_SRC/CMakeLists.txt.save ]; then
	mv $HARFBUZZ_HTML5_SRC/CMakeLists.txt $HARFBUZZ_HTML5_SRC/CMakeLists.txt.save
fi
REMOVE_MORE_ITEMS='# seems there are more test files that needs to be filtered out...
list(REMOVE_ITEM HARFBUZZ_SRC_FILES
	"\${CMAKE_CURRENT_SOURCE_DIR}/../src/dump-indic-data.cc"
	"\${CMAKE_CURRENT_SOURCE_DIR}/../src/dump-khmer-data.cc"
	"\${CMAKE_CURRENT_SOURCE_DIR}/../src/dump-myanmar-data.cc"
	"\${CMAKE_CURRENT_SOURCE_DIR}/../src/dump-use-data.cc"
	"\${CMAKE_CURRENT_SOURCE_DIR}/../src/test-iter.cc"
	"\${CMAKE_CURRENT_SOURCE_DIR}/../src/test-unicode-ranges.cc"
	)

'
cat $HARFBUZZ_HTML5_SRC/CMakeLists.txt.save | perl -0p -e "s!(# Include)!$REMOVE_MORE_ITEMS\1!" > $HARFBUZZ_HTML5_SRC/CMakeLists.txt
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
		-DUSE_INTEL_ATOMIC_PRIMITIVES=ON \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_CXX_FLAGS="-std=c++11 -DU_DISABLE_VERSION_SUFFIX=0" \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS" \
		-DCMAKE_CXX_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS" \
		"$HARFBUZZ_HTML5_SRC"
	cmake --build . -- harfbuzz -j VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	cp ../libharfbuzz.$UE_LIB_EXT "$HARFBUZZ_HTML5_DST"/lib/libharfbuzz${SUFFIX}.$UE_LIB_EXT
	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l "$HARFBUZZ_HTML5_DST/lib"

