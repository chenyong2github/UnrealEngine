// Copyright Epic Games, Inc. All Rights Reserved.
#include "ClothPhysicalMeshDataNv_Legacy.h"
#include "ClothPhysicalMeshData.h"  // For EWeightMapTargetCommon

UClothPhysicalMeshDataNv_Legacy::UClothPhysicalMeshDataNv_Legacy()
{
	Super::RegisterFloatArray((uint32)EWeightMapTargetCommon::MaxDistance, &MaxDistances);
	Super::RegisterFloatArray((uint32)EWeightMapTargetCommon::BackstopDistance, &BackstopDistances);
	Super::RegisterFloatArray((uint32)EWeightMapTargetCommon::BackstopRadius, &BackstopRadiuses);
	Super::RegisterFloatArray((uint32)EWeightMapTargetCommon::AnimDriveStiffness, &AnimDriveMultipliers);
}

UClothPhysicalMeshDataNv_Legacy::~UClothPhysicalMeshDataNv_Legacy()
{}
