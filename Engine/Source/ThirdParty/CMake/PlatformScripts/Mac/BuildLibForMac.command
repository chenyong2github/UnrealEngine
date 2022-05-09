#!/bin/zsh -e
# Copyright Epic Games, Inc. All Rights Reserved.

# Usage: BuildLibForMac.command <LibName> <LibVersion> [--config=Config] [--arch=Arch]

zmodload zsh/zutil

LIB_NAME=${1:?Missing library name argument}
LIB_VERSION=${2:?Missing library version argument}
LIB_CONFIGS=(Debug Release)
LIB_ARCHS=(arm64 x86_64)
LIB_DEPLOYMENT_TARGET=(10.14)
LIB_MAKE_TARGET=("")
LIB_OUTPUT_NAME=(${LIB_NAME})

zparseopts -D -E -K -                         \
	-config+:=LIB_CONFIGS                     \
	-arch+:=LIB_ARCHS                         \
	-deployment-target:=LIB_DEPLOYMENT_TARGET \
	-make-target:=LIB_MAKE_TARGET             \
	-output-name:=LIB_OUTPUT_NAME             \
	|| exit 1

LIB_CONFIGS=(${LIB_CONFIGS#--config})
LIB_CONFIGS=(${LIB_CONFIGS#=})
LIB_ARCHS=(${LIB_ARCHS#--arch})
LIB_ARCHS=(${LIB_ARCHS#=})
LIB_DEPLOYMENT_TARGET=${LIB_DEPLOYMENT_TARGET[-1]#=}
LIB_MAKE_TARGET=${LIB_MAKE_TARGET[-1]#=}
LIB_OUTPUT_NAME=${LIB_OUTPUT_NAME[-1]#=}

LIB_DIR=${0:a:h:h:h:h}/${LIB_NAME}/${LIB_VERSION}/lib/Mac
ENGINE_ROOT=${0:a:h:h:h:h:h:h}

for Config in ${LIB_CONFIGS}; do
	for Arch in ${LIB_ARCHS}; do
		echo "Building ${LIB_NAME}-${LIB_VERSION} for Mac ${Arch} in ${Config}..."
		"${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=Mac -TargetArchitecture=${Arch} -TargetLib=${LIB_NAME} -TargetLibVersion=${LIB_VERSION} -TargetConfigs=${Config} -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget="${LIB_MAKE_TARGET}" -CMakeAdditionalArguments="-DCMAKE_OSX_DEPLOYMENT_TARGET=${LIB_DEPLOYMENT_TARGET}" -SkipCreateChangelist
	done
	mkdir -p ${LIB_DIR}/${Config}
	lipo -create -output ${LIB_DIR}/${Config}/lib${LIB_OUTPUT_NAME}.a ${LIB_DIR}/${^LIB_ARCHS}/${Config}/lib${LIB_OUTPUT_NAME}.a
	rm -rf ${LIB_DIR}/${^LIB_ARCHS}
done
