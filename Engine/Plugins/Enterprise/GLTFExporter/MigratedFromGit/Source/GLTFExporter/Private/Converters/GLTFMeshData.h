// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"
#include "MaterialBakingStructures.h"

struct FGLTFMeshData
{
	FGLTFMeshData(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex);
	FGLTFMeshData(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex);

	const FGLTFMeshData* GetParent() const;

	FString Name;
	FPrimitiveData PrimitiveData;
	FMeshDescription Description;

	FLightMapRef LightMap;
	const FLightmapResourceCluster* LightMapResourceCluster;
	int32 LightMapTexCoord;

	int32 BakeUsingTexCoord;

	// TODO: find a better name for referencing the mesh-only data (no component)
	const FGLTFMeshData* Parent;
};
