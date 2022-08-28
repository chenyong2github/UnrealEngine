// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshSection.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Algo/MaxElement.h"

FGLTFMeshSection::FGLTFMeshSection(const FStaticMeshLODResources* MeshLOD, const FGLTFIndexArray& SectionIndices)
{
	const FRawStaticIndexBuffer& SourceBuffer = MeshLOD->IndexBuffer;
	if (SourceBuffer.Is32Bit())
	{
		Init(MeshLOD->Sections, SectionIndices, SourceBuffer.AccessStream32());
	}
	else
	{
		Init(MeshLOD->Sections, SectionIndices, SourceBuffer.AccessStream16());
	}
}

FGLTFMeshSection::FGLTFMeshSection(const FSkeletalMeshLODRenderData* MeshLOD, const FGLTFIndexArray& SectionIndices)
{
	const FMultiSizeIndexContainer& SourceBuffer = MeshLOD->MultiSizeIndexContainer;
	const void* SourceData = const_cast<FRawStaticIndexBuffer16or32Interface*>(SourceBuffer.GetIndexBuffer())->GetPointerTo(0);

	if (SourceBuffer.GetDataTypeSize() != sizeof(uint16))
	{
		Init(MeshLOD->RenderSections, SectionIndices, static_cast<const uint32*>(SourceData));
	}
	else
	{
		Init(MeshLOD->RenderSections, SectionIndices, static_cast<const uint16*>(SourceData));
	}
}

template <typename IndexType, typename SectionArrayType>
void FGLTFMeshSection::Init(const SectionArrayType& Sections, const FGLTFIndexArray& SectionIndices, const IndexType* SourceData)
{
	uint32 TotalIndexCount = 0;
	for (const int32 SectionIndex : SectionIndices)
	{
		TotalIndexCount += Sections[SectionIndex].NumTriangles * 3;
	}

	IndexMap.Reserve(TotalIndexCount);
	IndexBuffer.AddUninitialized(TotalIndexCount);
	BoneMapLookup.Reserve(TotalIndexCount);
	MaxBoneIndex = 0;

	TMap<uint32, uint32> IndexLookup;

	for (const int32 SectionIndex : SectionIndices)
	{
		const typename SectionArrayType::ElementType& Section = Sections[SectionIndex];
		const TArray<FBoneIndexType>& BoneMap = GetBoneMap(Section);

		const uint32 IndexOffset = GetIndexOffset(Section);
		const uint32 IndexCount = Section.NumTriangles * 3;
		const uint32 BoneMapIndex = BoneMaps.Num();

		for (uint32 Index = 0; Index < IndexCount; Index++)
		{
			const uint32 OldIndex = SourceData[IndexOffset + Index];
			uint32 NewIndex;

			if (const uint32* FoundIndex = IndexLookup.Find(OldIndex))
			{
				NewIndex = *FoundIndex;
			}
			else
			{
				NewIndex = IndexMap.Num();
				IndexLookup.Add(OldIndex, NewIndex);
				IndexMap.Add(OldIndex);

				if (BoneMap.Num() > 0)
				{
					BoneMapLookup.Add(BoneMapIndex);
				}
			}

			IndexBuffer[Index] = NewIndex;
		}

		if (BoneMap.Num() > 0)
		{
			BoneMaps.Add(BoneMap);

			if (const FBoneIndexType* MaxSectionBoneIndex = Algo::MaxElement(BoneMap))
			{
				MaxBoneIndex = FMath::Max(*MaxSectionBoneIndex, MaxBoneIndex);
			}
		}
	}
}

uint32 FGLTFMeshSection::GetIndexOffset(const FStaticMeshSection& Section)
{
	return Section.FirstIndex;
}

uint32 FGLTFMeshSection::GetIndexOffset(const FSkelMeshRenderSection& Section)
{
	return Section.BaseIndex;
}

const TArray<FBoneIndexType>& FGLTFMeshSection::GetBoneMap(const FStaticMeshSection& Section)
{
	static const TArray<FBoneIndexType> BoneMap = {};
	return BoneMap;
}

const TArray<FBoneIndexType>& FGLTFMeshSection::GetBoneMap(const FSkelMeshRenderSection& Section)
{
	return Section.BoneMap;
}
