// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFMeshSection
{
	FGLTFMeshSection(const FStaticMeshLODResources* MeshLOD, const TArray<int32>& SectionIndices);
	FGLTFMeshSection(const FSkeletalMeshLODRenderData* MeshLOD, const TArray<int32>& SectionIndices);

	TArray<uint32> IndexMap;
	TArray<uint32> IndexBuffer;

	TArray<TArray<FBoneIndexType>> BoneMaps;
	TArray<uint32> BoneMapLookup;
	FBoneIndexType MaxBoneIndex;
};
