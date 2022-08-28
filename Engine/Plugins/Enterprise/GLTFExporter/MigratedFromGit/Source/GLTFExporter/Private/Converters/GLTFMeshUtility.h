// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFIndexArray.h"

class FSkeletalMeshLODRenderData;

struct FGLTFMeshUtility
{
	static const TArray<FStaticMaterial>& GetMaterials(const UStaticMesh* StaticMesh);
	static const TArray<FSkeletalMaterial>& GetMaterials(const USkeletalMesh* SkeletalMesh);

	static FGLTFIndexArray GetSectionIndices(const FStaticMeshLODResources& MeshLOD, int32 MaterialIndex);
	static FGLTFIndexArray GetSectionIndices(const FSkeletalMeshLODRenderData& MeshLOD, int32 MaterialIndex);

	static int32 GetLOD(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 DefaultLOD);
	static int32 GetLOD(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 DefaultLOD);

	static int32 GetMaximumLOD(const UStaticMesh* StaticMesh);
	static int32 GetMaximumLOD(const USkeletalMesh* SkeletalMesh);

	static int32 GetMinimumLOD(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent);
	static int32 GetMinimumLOD(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent);
};
