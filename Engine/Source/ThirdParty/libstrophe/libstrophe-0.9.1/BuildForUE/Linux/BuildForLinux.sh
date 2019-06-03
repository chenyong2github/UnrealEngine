#!/bin/bash

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

SCRIPT_DIR=$(cd $(dirname $0) && pwd)

for BUILD_CONFIG in Debug Release; do
	BUILD_DIR="${SCRIPT_DIR}/../../Linux/Build-${BUILD_CONFIG}"

	if [ -d "${BUILD_DIR}" ]; then
		rm -rf "${BUILD_DIR}"
	fi
	mkdir -pv "${BUILD_DIR}"

	pushd "${BUILD_DIR}"
	cmake3 -DSOCKET_IMPL=../../src/sock.c -DCMAKE_BUILD_TYPE="${BUILD_CONFIG}" -DDISABLE_TLS=0 -DOPENSSL_PATH=../../../OpenSSL/1_0_2h/include/Linux/x86_64-unknown-linux-gnu -DCMAKE_OSX_DEPLOYMENT_TARGET="10.9" "${SCRIPT_DIR}/../../BuildForUE"

	make -j8

	OUTPUT_DIR="${SCRIPT_DIR}/../../Linux/x86_64-unknown-linux-gnu/${BUILD_CONFIG}"
	mkdir -p "${OUTPUT_DIR}"
	mv "${SCRIPT_DIR}/../../Linux/libstrophe.a" "${OUTPUT_DIR}"

	popd

	rm -rf "${BUILD_DIR}"
	
done

# -fPIC version
for BUILD_CONFIG in Debug Release; do
	BUILD_DIR="${SCRIPT_DIR}/../../Linux/Build-${BUILD_CONFIG}"

	if [ -d "${BUILD_DIR}" ]; then
		rm -rf "${BUILD_DIR}"
	fi
	mkdir -pv "${BUILD_DIR}"

	pushd "${BUILD_DIR}"
	cmake3 -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=true -DSOCKET_IMPL=../../src/sock.c -DCMAKE_BUILD_TYPE="${BUILD_CONFIG}" -DDISABLE_TLS=0 -DOPENSSL_PATH=../../../OpenSSL/1_0_2h/include/Linux/x86_64-unknown-linux-gnu -DCMAKE_OSX_DEPLOYMENT_TARGET="10.9" "${SCRIPT_DIR}/../../BuildForUE"

	make -j8

	OUTPUT_DIR="${SCRIPT_DIR}/../../Linux/x86_64-unknown-linux-gnu/${BUILD_CONFIG}"
	mkdir -p "${OUTPUT_DIR}"
	mv "${SCRIPT_DIR}/../../Linux/libstrophe.a" "${OUTPUT_DIR}/libstrophe_fPIC.a"

	popd

	rm -rf "${BUILD_DIR}"
	
done
