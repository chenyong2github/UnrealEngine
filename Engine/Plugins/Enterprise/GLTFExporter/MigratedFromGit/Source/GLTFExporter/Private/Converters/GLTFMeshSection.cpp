// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshSection.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/SkeletalMeshRenderData.h"

FGLTFMeshSection::FGLTFMeshSection(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* OldIndexBuffer)
{
	const uint32 IndexOffset = MeshSection->FirstIndex;
	const uint32 IndexCount = MeshSection->NumTriangles * 3;

	IndexMap.Reserve(IndexCount);
	IndexBuffer.AddUninitialized(IndexCount);

	TMap<uint32, uint32> IndexLookup;

	for (uint32 Index = 0; Index < IndexCount; Index++)
	{
		const uint32 OldIndex = OldIndexBuffer->GetIndex(IndexOffset + Index);
		uint32 NewIndex;

		if (uint32* FoundIndex = IndexLookup.Find(OldIndex))
		{
			NewIndex = *FoundIndex;
		}
		else
		{
			NewIndex = IndexMap.Num();
			IndexLookup.Add(OldIndex, NewIndex);
			IndexMap.Add(OldIndex);
		}

		IndexBuffer[Index] = NewIndex;
	}
}

FGLTFMeshSection::FGLTFMeshSection(const FSkelMeshRenderSection* MeshSection, const FRawStaticIndexBuffer16or32Interface* OldIndexBuffer)
{
	const uint32 IndexOffset = MeshSection->BaseIndex;
	const uint32 IndexCount = MeshSection->NumTriangles * 3;

	IndexMap.Reserve(IndexCount);
	IndexBuffer.AddUninitialized(IndexCount);

	TMap<uint32, uint32> IndexLookup;

	for (uint32 Index = 0; Index < IndexCount; Index++)
	{
		const uint32 OldIndex = OldIndexBuffer->Get(IndexOffset + Index);
		uint32 NewIndex;

		if (uint32* FoundIndex = IndexLookup.Find(OldIndex))
		{
			NewIndex = *FoundIndex;
		}
		else
		{
			NewIndex = IndexMap.Num();
			IndexLookup.Add(OldIndex, NewIndex);
			IndexMap.Add(OldIndex);
		}

		IndexBuffer[Index] = NewIndex;
	}

	BoneMap = MeshSection->BoneMap;
}
