// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFStaticMeshConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Builders/GLTFConvertBuilder.h"

FGLTFJsonBufferViewIndex FGLTFIndexBufferConverter::Convert(const FString& Name, const FRawStaticIndexBuffer* IndexBuffer)
{
	if (IndexBuffer->GetNumIndices() == 0)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	if (IndexBuffer->Is32Bit())
	{
		TArray<uint32> Indices;
		IndexBuffer->GetCopy(Indices);
		return Builder.AddBufferView(Indices, Name, sizeof(uint32), EGLTFJsonBufferTarget::ElementArrayBuffer);
	}
	else
	{
		const uint16* IndexData = IndexBuffer->AccessStream16();
		const int32 IndexDataSize = IndexBuffer->GetIndexDataSize();
		return Builder.AddBufferView(IndexData, IndexDataSize, Name, sizeof(uint16), EGLTFJsonBufferTarget::ElementArrayBuffer);
	}
}

FGLTFJsonAccessorIndex FGLTFStaticMeshSectionConverter::Convert(const FString& Name, const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer)
{
	const uint32 TriangleCount = MeshSection->NumTriangles;
	if (TriangleCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const uint32 FirstIndex = MeshSection->FirstIndex;
	const bool bIs32Bit = IndexBuffer->Is32Bit();

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.GetOrAddIndexBufferView(IndexBuffer);
	JsonAccessor.ByteOffset = FirstIndex * (bIs32Bit ? sizeof(uint32) : sizeof(uint16));
	JsonAccessor.ComponentType = bIs32Bit ? EGLTFJsonComponentType::U32 : EGLTFJsonComponentType::U16;
	JsonAccessor.Count = TriangleCount * 3;
	JsonAccessor.Type = EGLTFJsonAccessorType::Scalar;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(const FString& Name, const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials)
{
	if (LODIndex < 0 || StaticMesh->GetNumLODs() <= LODIndex)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);

	FGLTFJsonMesh JsonMesh;
	JsonMesh.Name = Name;

	const FPositionVertexBuffer* PositionBuffer = &MeshLOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &MeshLOD.VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = OverrideVertexColors != nullptr ? OverrideVertexColors : &MeshLOD.VertexBuffers.ColorVertexBuffer;

	FGLTFJsonAttributes JsonAttributes;
	JsonAttributes.Position = Builder.GetOrAddPositionAccessor(PositionBuffer, Name + TEXT("_Positions"));

	if (Builder.ExportOptions->bExportVertexColors)
	{
		JsonAttributes.Color0 = Builder.GetOrAddColorAccessor(ColorBuffer, Name + TEXT("_Colors"));
	}

	JsonAttributes.Normal = Builder.GetOrAddNormalAccessor(VertexBuffer, Name + TEXT("_Normals"));
	JsonAttributes.Tangent = Builder.GetOrAddTangentAccessor(VertexBuffer, Name + TEXT("_Tangents"));

	const uint32 UVCount = VertexBuffer->GetNumTexCoords();
	JsonAttributes.TexCoords.AddUninitialized(UVCount);

	for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
	{
		JsonAttributes.TexCoords[UVIndex] = Builder.GetOrAddUVAccessor(VertexBuffer, UVIndex, Name + TEXT("_UV") + FString::FromInt(UVIndex) + TEXT("s"));
	}

	const FRawStaticIndexBuffer* IndexBuffer = &MeshLOD.IndexBuffer;
	Builder.GetOrAddIndexBufferView(IndexBuffer, Name + TEXT("_Indices"));

	const int32 SectionCount = MeshLOD.Sections.Num();
	JsonMesh.Primitives.AddDefaulted(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[SectionIndex];
		JsonPrimitive.Attributes = JsonAttributes;

		const FStaticMeshSection& Section = MeshLOD.Sections[SectionIndex];
		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(&Section, IndexBuffer,
			Name + (SectionCount == 1 ? TEXT("_Indices") : TEXT("_Indices_Section") + FString::FromInt(SectionIndex)));

		const int32 MaterialIndex = Section.MaterialIndex;
		const UMaterialInterface* Material = OverrideMaterials.IsValidIndex(MaterialIndex) && OverrideMaterials[MaterialIndex] != nullptr ?
			OverrideMaterials[MaterialIndex] : StaticMesh->GetMaterial(MaterialIndex);

		if (Material != nullptr)
		{
			JsonPrimitive.Material = Builder.GetOrAddMaterial(Material);
		}
	}

	return Builder.AddMesh(JsonMesh);
}
