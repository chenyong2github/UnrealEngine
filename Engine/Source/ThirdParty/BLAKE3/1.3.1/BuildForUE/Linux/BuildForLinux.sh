#!/bin/bash -e
# Copyright Epic Games, Inc. All Rights Reserved.

TARGET=${1:?Missing target architecture triple}

SELF_DIR=$(readlink -f "$(dirname "$BASH_SOURCE")")

SRC_DIR=${SELF_DIR}/..
LIB_DIR=${SRC_DIR}/../lib/Linux/${TARGET}
INT_DIR=${2:-${SELF_DIR}/Build}

if [ $UID -eq 0 ]; then
	yum install -y clang cmake gcc-c++ make
	adduser build
	exec su build "$0" "$1" /tmp/build
fi

mkdir -p "${INT_DIR}"
cd "${INT_DIR}"

cmake "${SRC_DIR}" -G "Unix Makefiles" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER_TARGET=${TARGET} -DCMAKE_CXX_COMPILER_TARGET=${TARGET} -DCMAKE_C_FLAGS="-fPIC -gdwarf-4 -O3" -DCMAKE_INSTALL_PREFIX="${LIB_DIR}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="${LIB_DIR}/Release"
cmake --build . --config Release
rm -rf "${INT_DIR}"
