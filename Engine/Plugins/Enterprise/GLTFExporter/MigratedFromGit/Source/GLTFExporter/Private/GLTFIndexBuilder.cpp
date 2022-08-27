// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFIndexBuilder.h"
#include "GLTFContainerBuilder.h"
#include "GLTFMeshBuilder.h"

FGLTFJsonMeshIndex FGLTFIndexBuilder::Find(const FGLTFStaticMeshKey& Key) const
{
	const FGLTFJsonMeshIndex* Index = StaticMeshes.Find(Key);
	return Index != nullptr ? *Index : FGLTFJsonMeshIndex(INDEX_NONE);
}

FGLTFJsonMeshIndex FGLTFIndexBuilder::FindOrAdd(const FGLTFStaticMeshKey& Key, FGLTFContainerBuilder& Container)
{
	FGLTFJsonMeshIndex Index = Find(Key);

	if (Index == INDEX_NONE)
	{
		Index = FGLTFMeshBuilder(Key.StaticMesh, Key.LODIndex, Key.OverrideVertexColors).AddMesh(Container);
		StaticMeshes.Add(Key, Index);
	}
	
	return Index;
}
