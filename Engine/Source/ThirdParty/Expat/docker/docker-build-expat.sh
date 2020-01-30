#!/bin/bash

## Unreal Engine 4 Build script for Expat
## Copyright Epic Games, Inc. All Rights Reserved.

# Should be run in docker image, launched something like this (see RunMe.sh script):
#   docker run --name ${ImageName} -v ${SCRIPT_DIR}:/src ${Image} /src/docker-build-expat.sh
#
# Built expat libraries are in /build directory

if [ $UID -eq 0 ]; then
  # Centos 7
  yum install -y cmake make gcc-c++

  # Create non-privileged user and workspace
  adduser buildmaster
  mkdir -p /build
  chown buildmaster:nobody -R /build
  cd /build

  exec su buildmaster "$0"
fi

# This will be run from user buildmaster

export EXPAT_DIR=/expat

# Get num of cores
export CORES=$(getconf _NPROCESSORS_ONLN)
echo Using ${CORES} cores for building

BuildWithOptions()
{
	local BuildDir=$1
	shift 1
	local Options="$@"

	rm -rf $BuildDir
	mkdir -p $BuildDir
	pushd $BuildDir

	cmake $Options ${EXPAT_DIR}
	make -j${CORES}

	popd
}

set -e

BuildWithOptions Debug   -DCMAKE_BUILD_TYPE=Debug   -DCMAKE_C_FLAGS="-fPIC -gdwarf-4" -DBUILD_tools=OFF -DBUILD_examples=OFF -DBUILD_tests=OFF -DBUILD_shared=OFF -DSKIP_PRE_BUILD_COMMAND=TRUE
BuildWithOptions Release -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-fPIC -gdwarf-4" -DBUILD_tools=OFF -DBUILD_examples=OFF -DBUILD_tests=OFF -DBUILD_shared=OFF -DSKIP_PRE_BUILD_COMMAND=TRUE

set +e

