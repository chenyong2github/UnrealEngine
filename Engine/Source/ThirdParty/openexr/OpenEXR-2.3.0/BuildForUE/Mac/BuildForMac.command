#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.

## 
## Most of the following script is intended to be consistent for building all Mac 
## third-party source. The sequence of steps are -
## 1) Set up constants, create temp dir, checkout files, save file info
## 2) lib-specific build steps
## 3) Check files were updated

##
## Lib specific constants

# Name of lib
LIB_NAME="openexr"
# Drops from the location of this script to where libfiles are relative to
#  e.g.
#  {DROP_TO_LIBROOT}/README
#  {DROP_TO_LIBROOT}/include)
#  ${DROP_TO_LIBROOT}/$LIBFILES[0])
DROP_TO_LIBROOT=../..
# Drops from the location of LIBROOT to Engine/Source/ThirdParrty
DROP_TO_THIRDPARTY=../..

# libs are under a Deploy folder that's a sibling of source
DEPLOYED_LIBS=../Deploy/OpenEXR-2.3.0/OpenEXR/lib/Mac/StaticRelease

# files we build, relative to LIBROOT
LIBFILES=( "${DEPLOYED_LIBS}/libHalf.a" "${DEPLOYED_LIBS}/libIex.a" "${DEPLOYED_LIBS}/libIexMath.a" "${DEPLOYED_LIBS}/libIlmImf.a" "${DEPLOYED_LIBS}/libIlmImfUtil.a" "${DEPLOYED_LIBS}/libIlmThread.a" "${DEPLOYED_LIBS}/libImath.a")

## Common setup steps

# Build script will be in <lib>/Build/Mac so get that path and drop two folders to leave us
# in the actual lib folder
pushd . > /dev/null
SCRIPT_DIR="`dirname "${BASH_SOURCE[0]}"`"
cd ${SCRIPT_DIR}/${DROP_TO_LIBROOT}
LIB_ROOT_DIR=${PWD}
echo Changed to ${LIB_ROOT_DIR}

# We should be in ThirdParty/LibName and we want to pull in some common things from
# ThirdParty/BuildScripts/Mac/Common
source ${DROP_TO_THIRDPARTY}/BuildScripts/Mac/Common/Common.sh

# create a tempdir and save it (note the tmpdir variable is used by the functions that 
# check file state)
TMPDIR="/tmp/${LIB_NAME}-$$"
mkdir -p ${TMPDIR} > /dev/null 2>&1

echo Rebuilding ${LIB_NAME} using temp path ${TMP_DIR}

# checkout the library list and save their state
checkoutFiles ${LIBFILES[@]}
saveFileStates ${LIBFILES[@]}

## 
## OpenEXR Specific Steps for automake

BUILD_ARCH="x86_64"
BUILD_FLAGS="-arch x86_64"

if [ "$BUILD_UNIVERSAL" = true ] ; then
    BUILD_ARCH="${BUILD_ARCH} arm64"
    BUILD_FLAGS="${BUILD_FLAGS}  -arch arm64"
fi

# Unreal uses the dwarf-2 format.  Updating it will require removing '-gdwarf-2' from:
#   Engine/Source/Programs/UnrealBuildTool/Platform/Mac/MacToolChain.cs
export CFLAGS="-gdwarf-2"
export CXXFLAGS="-gdwarf-2"

# make ilmbase and install it to tmp
cd ${LIB_ROOT_DIR}/IlmBase
sh ./bootstrap
sh ./configure --prefix=${TMPDIR} --enable-osx-arch="${BUILD_ARCH}" --disable-dependency-tracking
make clean && make -j$(get_core_count) && make install

# make OpenEXR (which depends on IlmBase) and install it to tmp
cd ${LIB_ROOT_DIR}/OpenEXR
sh ./bootstrap
# OpenEXR does not recognize the --enable-osx-arch option that IlmBase does, so set those flags
# manually
export CXXFLAGS="${BUILD_FLAGS}"
sh ./configure --prefix=${TMPDIR} --with-ilmbase-prefix=${TMPDIR} --disable-dependency-tracking
make clean && make -j$(get_core_count) && make install

# back to where our libs are relative to
cd ${LIB_ROOT_DIR} 

# Copy the built libs from the temp directory over to the deploy folder
cp ${TMPDIR}/lib/*.a ${DEPLOYED_LIBS}/

# check the files were all touched
checkFilesWereUpdated ${LIBFILES[@]}

checkFilesAreFatBinaries ${LIBFILES[@]}

echo The following files were rebuilt: ${LIBFILES[@]}

popd > /dev/null