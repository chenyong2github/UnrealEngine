#!/bin/bash
set -x
# set -x -e # ICU has a number of warnings that will fail when -e is set


# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Platforms/HTML5/Build/BatchFiles/Build_All_HTML5_libs.sh


ICU_HTML5=$(pwd)
ICU_VERSION='icu4c-64_1'
ICU_HTML5_SRC="$UE4_TPS_SRC/ICU/$ICU_VERSION/source"
ICU_HTML5_DST="$HTML5_TPS_LIBS/ICU/$ICU_VERSION"


# local destination
if [ ! -d "$ICU_HTML5_DST/lib" ]; then
	mkdir -p "$ICU_HTML5_DST/lib"
fi
# TODO remove this p4 hack after HTML5 becomes community driven only
if [ ! -z "$(ls -A "$ICU_HTML5_DST/lib")" ]; then
	chmod +w "$ICU_HTML5_DST"/lib/*
fi


# ----------------------------------------
# WASM does not support SIMD instructions yet
if [ ! -e "$ICU_HTML5_SRC/i18n/double-conversion.cpp.save" ]; then
	mv "$ICU_HTML5_SRC/i18n/double-conversion.cpp" "$ICU_HTML5_SRC/i18n/double-conversion.cpp.save"
fi
REPLACE_CODE='#ifdef __EMSCRIPTEN__
        return junk_string_value_;
#else
\1
#endif'
cat "$ICU_HTML5_SRC/i18n/double-conversion.cpp.save" | perl -0p -e "s/(.*Double::NaN.*)/$REPLACE_CODE/" > "$ICU_HTML5_SRC/i18n/double-conversion.cpp"
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
# BUG: https://github.com/tdlib/td/issues/612#issuecomment-507036835
# and is still happening as of emscripten 1.38.45
#	EMFLAGS="$UE_EMFLAGS -fno-except"
	# ----------------------------------------
	emcmake cmake -G "Unix Makefiles" \
		-DBUILD_SHARED_LIBS=OFF \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS" \
		-DCMAKE_CXX_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS" \
		"$ICU_HTML5_SRC/../BuildForUE"
	cmake --build . -- icu -j VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	cp ../libicu.$UE_LIB_EXT "$ICU_HTML5_DST"/lib/libicu${SUFFIX}.$UE_LIB_EXT
	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l "$ICU_HTML5_DST/lib"

