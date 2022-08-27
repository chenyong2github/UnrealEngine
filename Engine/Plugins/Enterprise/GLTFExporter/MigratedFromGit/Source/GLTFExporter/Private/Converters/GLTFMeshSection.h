// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFMeshSection
{
	FGLTFMeshSection(const FStaticMeshLODResources* MeshLOD, const int32 MaterialIndex);
	FGLTFMeshSection(const FSkeletalMeshLODRenderData* MeshLOD, const uint16 MaterialIndex);

	TArray<uint32> IndexMap;
	TArray<uint32> IndexBuffer;

	TArray<TArray<FBoneIndexType>> BoneMaps;
	TArray<uint32> BoneMapLookup;
	uint32 MaxBoneIndex;
};
