#!/bin/zsh -e
# Copyright Epic Games, Inc. All Rights Reserved.

# Usage: BuildLibForIOS.command <LibName> <LibVersion> [Configs] [Archs] [DeploymentTarget]

LIB_NAME=${1:?Missing library name argument}
LIB_VERSION=${2:?Missing library version argument}
LIB_CONFIGS=(${=3:-Debug Release})
LIB_ARCHS=(${=4:-arm64 x86_64})
LIB_DEPLOYMENT_TARGET=${5:-11.0}
LIB_DIR=${0:a:h:h:h:h}/${LIB_NAME}/${LIB_VERSION}/lib/IOS
ENGINE_ROOT=${0:a:h:h:h:h:h:h}

for Config in ${LIB_CONFIGS}; do
	for Arch in ${LIB_ARCHS}; do
		echo "Building ${LIB_NAME}-${LIB_VERSION} for iOS ${Arch} in ${Config}..."
		${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command BuildCMakeLib -TargetPlatform=IOS -TargetArchitecture=${Arch} -TargetLib=${LIB_NAME} -TargetLibVersion=${LIB_VERSION} -TargetConfigs=${Config} -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="-DCMAKE_OSX_DEPLOYMENT_TARGET=${LIB_DEPLOYMENT_TARGET}" -SkipCreateChangelist
	done
	mkdir -p ${LIB_DIR}/${Config}
	lipo -create -output ${LIB_DIR}/${Config}/lib${LIB_NAME}.a ${LIB_DIR}/${^LIB_ARCHS}/${Config}/lib${LIB_NAME}.a
	rm -rf ${LIB_DIR}/${^LIB_ARCHS}
done
