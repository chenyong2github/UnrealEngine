// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshUtility.h"
#include "PlatformInfo.h"

int32 FGLTFMeshUtility::GetMinimumLOD(const UStaticMesh* StaticMesh)
{
	return GetValueForRunningPlatform<int32>(StaticMesh->MinLOD);
}

int32 FGLTFMeshUtility::GetMinimumLOD(const USkeletalMesh* SkeletalMesh)
{
	return GetValueForRunningPlatform<int32>(SkeletalMesh->MinLod);
}

template <typename ValueType, typename StructType>
ValueType FGLTFMeshUtility::GetValueForRunningPlatform(const StructType& Properties)
{
	const PlatformInfo::FPlatformInfo& PlatformInfo = GetTargetPlatformManagerRef().GetRunningTargetPlatform()->GetPlatformInfo();
	return Properties.GetValueForPlatformIdentifiers(PlatformInfo.PlatformGroupName, PlatformInfo.VanillaPlatformName);
}
