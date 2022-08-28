// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFMeshSection
{
	FGLTFMeshSection(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* OldIndexBuffer);
	FGLTFMeshSection(const FSkelMeshRenderSection* MeshSection, const FRawStaticIndexBuffer16or32Interface* OldIndexBuffer);

	TArray<uint32> IndexMap;
	TArray<uint32> IndexBuffer;
	TArray<FBoneIndexType> BoneMap;
};
