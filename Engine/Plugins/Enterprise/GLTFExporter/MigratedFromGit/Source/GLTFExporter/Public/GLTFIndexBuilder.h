// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "Engine.h"

struct FGLTFContainerBuilder;

struct FGLTFStaticMeshKey
{
	const UStaticMesh* StaticMesh;
	int32 LODIndex;
	const FColorVertexBuffer* OverrideVertexColors;

	FGLTFStaticMeshKey(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors)
		: StaticMesh(StaticMesh)
		, LODIndex(LODIndex)
		, OverrideVertexColors(OverrideVertexColors)
	{
	}

	FORCEINLINE bool operator==(const FGLTFStaticMeshKey& Other) const
	{
		return StaticMesh           == Other.StaticMesh
		    && LODIndex             == Other.LODIndex
			&& OverrideVertexColors == Other.OverrideVertexColors;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FGLTFStaticMeshKey& Other)
	{
		return HashCombine(GetTypeHash(Other.StaticMesh), HashCombine(GetTypeHash(Other.LODIndex), GetTypeHash(Other.OverrideVertexColors)));
	}
};

struct GLTFEXPORTER_API FGLTFIndexBuilder
{
	TMap<FGLTFStaticMeshKey, FGLTFJsonMeshIndex> StaticMeshes;

	FGLTFJsonMeshIndex Find(const FGLTFStaticMeshKey& Key) const;
	FGLTFJsonMeshIndex FindOrAdd(const FGLTFStaticMeshKey& Key, FGLTFContainerBuilder& Container);
};
