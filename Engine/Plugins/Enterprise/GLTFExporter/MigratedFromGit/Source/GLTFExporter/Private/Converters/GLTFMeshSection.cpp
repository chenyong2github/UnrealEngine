// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshSection.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Algo/MaxElement.h"

FGLTFMeshSection::FGLTFMeshSection(const FStaticMeshLODResources* MeshLOD, const int32 MaterialIndex)
{
	uint32 TotalIndexCount = 0;

	TArray<const FStaticMeshSection*> Sections;
	for (const FStaticMeshSection& Section : MeshLOD->Sections)
	{
		if (Section.MaterialIndex == MaterialIndex)
		{
			Sections.Add(&Section);
			TotalIndexCount += Section.NumTriangles * 3;
		}
	}

	IndexMap.Reserve(TotalIndexCount);
	IndexBuffer.AddUninitialized(TotalIndexCount);
	BoneMapLookup.AddUninitialized(TotalIndexCount);

	TMap<uint32, uint32> IndexLookup;

	for (const FStaticMeshSection* MeshSection : Sections)
	{
		const uint32 IndexOffset = MeshSection->FirstIndex;
		const uint32 IndexCount = MeshSection->NumTriangles * 3;

		for (uint32 Index = 0; Index < IndexCount; Index++)
		{
			const uint32 OldIndex = MeshLOD->IndexBuffer.GetIndex(IndexOffset + Index);
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
			BoneMapLookup[Index] = 0;
		}
	}

	BoneMaps.Add({});
	MaxBoneIndex = 0;
}

FGLTFMeshSection::FGLTFMeshSection(const FSkeletalMeshLODRenderData* MeshLOD, const uint16 MaterialIndex)
{
	uint32 TotalIndexCount = 0;

	TArray<const FSkelMeshRenderSection*> Sections;
	for (const FSkelMeshRenderSection& Section : MeshLOD->RenderSections)
	{
		if (Section.MaterialIndex == MaterialIndex)
		{
			Sections.Add(&Section);
			TotalIndexCount += Section.NumTriangles * 3;
		}
	}

	IndexMap.Reserve(TotalIndexCount);
	IndexBuffer.AddUninitialized(TotalIndexCount);
	BoneMapLookup.AddUninitialized(TotalIndexCount);

	TMap<uint32, uint32> IndexLookup;

	const FRawStaticIndexBuffer16or32Interface* OldIndexBuffer = MeshLOD->MultiSizeIndexContainer.GetIndexBuffer();

	for (const FSkelMeshRenderSection* MeshSection : Sections)
	{
		const uint32 IndexOffset = MeshSection->BaseIndex;
		const uint32 IndexCount = MeshSection->NumTriangles * 3;
		const uint32 BoneMapIndex = BoneMaps.Num();

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
			BoneMapLookup[Index] = BoneMapIndex;
		}

		BoneMaps.Add(MeshSection->BoneMap);
		MaxBoneIndex = MeshSection->BoneMap.Num() > 0 ? *Algo::MaxElement(MeshSection->BoneMap) : 0;
	}
}
