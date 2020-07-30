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
LIB_NAME="PhysX"
# Drops from the location of this script to where libfiles are relative to
#  e.g.
#  {DROP_TO_LIBROOT}/README
#  {DROP_TO_LIBROOT}/include)
#  ${DROP_TO_LIBROOT}/$LIBFILES[0])
DROP_TO_LIBROOT=../..
# Drops from the location of LIBROOT to Engine/Source/ThirdParrty
DROP_TO_THIRDPARTY=..

# Path to libs from libroot
LIB_PATH=Lib/Mac

# Drops to location of TP binaries
DLIB_PATH=${DROP_TO_THIRDPARTY}/../../Binaries/ThirdParty/PhysX3/Mac

# files we build
LIBFILES=( 
    "${DLIB_PATH}/libAPEX_Clothing.dylib"
    "${DLIB_PATH}/libAPEX_ClothingCHECKED.dylib"
    "${DLIB_PATH}/libAPEX_ClothingPROFILE.dylib"
    "${DLIB_PATH}/libAPEX_Destructible.dylib"
    "${DLIB_PATH}/libAPEX_DestructibleCHECKED.dylib"
    "${DLIB_PATH}/libAPEX_DestructiblePROFILE.dylib"
    "${DLIB_PATH}/libAPEX_Legacy.dylib"
    "${DLIB_PATH}/libAPEX_LegacyCHECKED.dylib"
    "${DLIB_PATH}/libAPEX_LegacyPROFILE.dylib"
    "${DLIB_PATH}/libAPEX_Loader.dylib"
    "${DLIB_PATH}/libAPEX_LoaderCHECKED.dylib"
    "${DLIB_PATH}/libAPEX_LoaderPROFILE.dylib"
    "${DLIB_PATH}/libApexFramework.dylib"
    "${DLIB_PATH}/libApexFrameworkCHECKED.dylib"
    "${DLIB_PATH}/libApexFrameworkPROFILE.dylib"
    "${DLIB_PATH}/libNvCloth.dylib"
    "${DLIB_PATH}/libNvClothCHECKED.dylib"
    "${DLIB_PATH}/libNvClothPROFILE.dylib"
    "${DLIB_PATH}/libPhysX3.dylib"
    "${DLIB_PATH}/libPhysX3CHECKED.dylib"
    "${DLIB_PATH}/libPhysX3Common.dylib"
    "${DLIB_PATH}/libPhysX3CommonCHECKED.dylib"
    "${DLIB_PATH}/libPhysX3CommonPROFILE.dylib"
    "${DLIB_PATH}/libPhysX3Cooking.dylib"
    "${DLIB_PATH}/libPhysX3CookingCHECKED.dylib"
    "${DLIB_PATH}/libPhysX3CookingPROFILE.dylib"
    "${DLIB_PATH}/libPhysX3PROFILE.dylib"
    "${DLIB_PATH}/libPxFoundation.dylib"
    "${DLIB_PATH}/libPxFoundationCHECKED.dylib"
    "${DLIB_PATH}/libPxFoundationPROFILE.dylib"
    "${DLIB_PATH}/libPxPvdSDK.dylib"
    "${DLIB_PATH}/libPxPvdSDKCHECKED.dylib"
    "${DLIB_PATH}/libPxPvdSDKPROFILE.dylib"
    "${LIB_PATH}/libApexCommon.a"
    "${LIB_PATH}/libApexCommonCHECKED.a"
    "${LIB_PATH}/libApexCommonPROFILE.a"
    "${LIB_PATH}/libApexShared.a"
    "${LIB_PATH}/libApexSharedCHECKED.a"
    "${LIB_PATH}/libApexSharedPROFILE.a"
    "${LIB_PATH}/libLowLevel.a"
    "${LIB_PATH}/libLowLevelAABB.a"
    "${LIB_PATH}/libLowLevelAABBCHECKED.a"
    "${LIB_PATH}/libLowLevelAABBPROFILE.a"
    "${LIB_PATH}/libLowLevelCHECKED.a"
    "${LIB_PATH}/libLowLevelCloth.a"
    "${LIB_PATH}/libLowLevelClothCHECKED.a"
    "${LIB_PATH}/libLowLevelClothPROFILE.a"
    "${LIB_PATH}/libLowLevelDynamics.a"
    "${LIB_PATH}/libLowLevelDynamicsCHECKED.a"
    "${LIB_PATH}/libLowLevelDynamicsPROFILE.a"
    "${LIB_PATH}/libLowLevelPROFILE.a"
    "${LIB_PATH}/libLowLevelParticles.a"
    "${LIB_PATH}/libLowLevelParticlesCHECKED.a"
    "${LIB_PATH}/libLowLevelParticlesPROFILE.a"
    "${LIB_PATH}/libNvParameterized.a"
    "${LIB_PATH}/libNvParameterizedCHECKED.a"
    "${LIB_PATH}/libNvParameterizedPROFILE.a"
    "${LIB_PATH}/libPhysX3CharacterKinematic.a"
    "${LIB_PATH}/libPhysX3CharacterKinematicCHECKED.a"
    "${LIB_PATH}/libPhysX3CharacterKinematicPROFILE.a"
    "${LIB_PATH}/libPhysX3Extensions.a"
    "${LIB_PATH}/libPhysX3ExtensionsCHECKED.a"
    "${LIB_PATH}/libPhysX3ExtensionsPROFILE.a"
    "${LIB_PATH}/libPhysX3Vehicle.a"
    "${LIB_PATH}/libPhysX3VehicleCHECKED.a"
    "${LIB_PATH}/libPhysX3VehiclePROFILE.a"
    "${LIB_PATH}/libPsFastXml.a"
    "${LIB_PATH}/libPsFastXmlCHECKED.a"
    "${LIB_PATH}/libPsFastXmlPROFILE.a"
    "${LIB_PATH}/libPxTask.a"
    "${LIB_PATH}/libPxTaskCHECKED.a"
    "${LIB_PATH}/libPxTaskPROFILE.a"
    "${LIB_PATH}/libRenderDebug.a"
    "${LIB_PATH}/libRenderDebugCHECKED.a"
    "${LIB_PATH}/libRenderDebugPROFILE.a"
    "${LIB_PATH}/libSceneQuery.a"
    "${LIB_PATH}/libSceneQueryCHECKED.a"
    "${LIB_PATH}/libSceneQueryPROFILE.a"
    "${LIB_PATH}/libSimulationController.a"
    "${LIB_PATH}/libSimulationControllerCHECKED.a"
    "${LIB_PATH}/libSimulationControllerPROFILE.a"
)

##
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

echo Rebuilding ${LIB_NAME}

# checkout the library list and save their state
checkoutFiles ${LIBFILES[@]}
saveFileStates ${LIBFILES[@]}

## 
## PhysX specific steps

# PhysX can be built via UAT, which now does all the grunt work of building for both architectures and 
# lipo'ing the results into universal binaries
cd ${DROP_TO_THIRDPARTY}/../../..
echo Changed to ${PWD}

./RunUAT.sh BuildPhysX -TargetPlatforms=Mac -TargetConfigs="profile+release+checked" -SkipCreateChangelist -SkipSubmit
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Failed to build PhysX libs"
    exit 1
fi

# back to the lib dir for final checks
cd ${LIB_ROOT_DIR}
echo Changed to ${PWD}

# check the files were all touched
checkFilesWereUpdated ${LIBFILES[@]}

checkFilesAreFatBinaries ${LIBFILES[@]}

echo The following files were rebuilt: ${LIBFILES[@]}

popd > /dev/null