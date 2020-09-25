#!/bin/bash

LIB_NAME="PLCrashReporter"

DROP_TO_LIBROOT=../..
DROP_TO_THIRDPARTY=../..

LIBFILES=(
	"lib/Mac/Release/libCrashReporter.a"
	"lib/Mac/Debug/libCrashReporter.a"
)

################################################################################
# Set up script env.
#

pushd . > /dev/null

SCRIPT_DIR="`dirname "${BASH_SOURCE[0]}"`"
cd ${SCRIPT_DIR}/${DROP_TO_LIBROOT}
LIB_ROOT_DIR=${PWD}

echo Changed to ${LIB_ROOT_DIR}

source ${DROP_TO_THIRDPARTY}/BuildScripts/Mac/Common/Common.sh

################################################################################
# Set up build env.
#

TMPDIR="/tmp/${LIB_NAME}-$$"
ISYSROOT=$(xcrun --sdk macosx --show-sdk-path)
ARCHFLAGS="-arch x86_64 -arch arm64"
PREFIXDIR=${TMPDIR}/Deploy

mkdir -p ${PREFIXDIR} > /dev/null 2>&1

echo Rebuilding ${LIB_NAME} using temp path ${TMPDIR}

################################################################################
# Checkout the library list and save their state
#

checkoutFiles ${LIBFILES[@]}
saveFileStates ${LIBFILES[@]}

################################################################################
# Build the protobuf-c dependency, if requested.
#

if [[ ! -z "${BUILD_PROTOBUF_C_DEPEND}" ]]; then
	PROTOBUFDEPS=(
		"Dependencies/protobuf/bin/protoc-c"
		"Dependencies/protobuf/include/protobuf-c/protobuf-c.h"
	)

	checkoutFiles ${PROTOBUFDEPS[@]}
	saveFileStates ${PROTOBUFDEPS[@]}

	pushd ${TMPDIR} > /dev/null

	# Pull down and build the protobuf and protobuf-c repositories
	git clone https://github.com/protocolbuffers/protobuf.git
	git clone https://github.com/protobuf-c/protobuf-c.git

	############################################################################
	# Build protobuf (as a dependency for protobuf-c)
	#

	pushd protobuf > /dev/null

	# Use a known good release/tag
	git checkout tags/v3.13.0 -b v3.13.0

	# Initialize the submodules
	git submodule update --init --recursive

	# General the build config
	./autogen.sh
	./configure --prefix=${PREFIXDIR} "CFLAGS=${ARCHFLAGS} -isysroot ${ISYSROOT}" "CXXFLAGS=${ARCHFLAGS} -isysroot ${ISYSROOT}" "LDFLAGS=${ARCHFLAGS}"

	# Build and install protobuf
	make -j`sysctl -n hw.ncpu`
	make install

	popd > /dev/null

	############################################################################
	# Build protobuf-c
	#

	pushd protobuf-c > /dev/null

	# Use a known good release/tag (that is compatible with the protobuf version)
	git checkout tags/v1.3.3 -b v1.3.3

	# General the build config
	./autogen.sh
	./configure --prefix=${PREFIXDIR} "CFLAGS=${ARCHFLAGS} -isysroot ${ISYSROOT}" "CXXFLAGS=${ARCHFLAGS} -isysroot ${ISYSROOT}" "LDFLAGS=${ARCHFLAGS}" "protobuf_CFLAGS=-I${PREFIXDIR}/include" "protobuf_LIBS=-L${PREFIXDIR}/lib -lprotobuf" "PKG_CONFIG_PATH=$PKG_CONFIG_PATH:${PREFIXDIR}/lib/pkgconfig"

	# Build and install protobuf-c
	make -j`sysctl -n hw.ncpu`
	make install

	popd > /dev/null

	# Return to LIB_ROOT_DIR
	popd > /dev/null

	############################################################################
	# Update Dependencies for the Xcode project
	#

	cp -a ${PREFIXDIR}/bin/protoc-gen-c Dependencies/protobuf/bin/protoc-c
	cp -a ${PREFIXDIR}/include/protobuf-c Dependencies/protobuf/include/

	cp -a ${PREFIXDIR}/lib/libprotobuf-c.a lib/Mac/Release
	cp -a ${PREFIXDIR}/lib/libprotobuf-c.a lib/Mac/Debug

	checkFilesWereUpdated ${PROTOBUFDEPS[@]}
	checkFilesAreFatBinaries ${PROTOBUFDEPS[0]}

	echo The protobuf-c dependency for ${LIB_NAME} was rebuilt
fi

################################################################################
# Build the PLCrashReporter Libraries

xcodebuild -project CrashReporter.xcodeproj -target CrashReporter-MacOSX-Static -configuration Debug clean
xcodebuild -project CrashReporter.xcodeproj -target CrashReporter-MacOSX-Static -configuration Debug

xcodebuild -project CrashReporter.xcodeproj -target CrashReporter-MacOSX-Static -configuration Release clean
xcodebuild -project CrashReporter.xcodeproj -target CrashReporter-MacOSX-Static -configuration Release

################################################################################
# Check that the files were all touched
#

checkFilesWereUpdated ${LIBFILES[@]}
checkFilesAreFatBinaries ${LIBFILES[@]}

echo The following files were rebuilt: ${LIBFILES[@]}

################################################################################
popd > /dev/null
