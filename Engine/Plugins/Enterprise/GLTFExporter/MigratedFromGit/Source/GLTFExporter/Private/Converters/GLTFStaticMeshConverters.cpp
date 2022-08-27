// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFStaticMeshConverters.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Converters/GLTFConverterUtility.h"

FGLTFJsonAccessorIndex FGLTFPositionVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FPositionVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector3> Positions;
	Positions.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Positions[VertexIndex] = FGLTFConverterUtility::ConvertPosition(VertexBuffer->VertexPosition(VertexIndex));
	}

	// More accurate bounding box if based on raw vertex values
	FGLTFJsonVector3 MinPosition = Positions[0];
	FGLTFJsonVector3 MaxPosition = Positions[0];

	for (uint32 VertexIndex = 1; VertexIndex < VertexCount; ++VertexIndex)
	{
		const FGLTFJsonVector3& Position = Positions[VertexIndex];
		MinPosition.X = FMath::Min(MinPosition.X, Position.X);
		MinPosition.Y = FMath::Min(MinPosition.Y, Position.Y);
		MinPosition.Z = FMath::Min(MinPosition.Z, Position.Z);
		MaxPosition.X = FMath::Max(MaxPosition.X, Position.X);
		MaxPosition.Y = FMath::Max(MaxPosition.Y, Position.Y);
		MaxPosition.Z = FMath::Max(MaxPosition.Z, Position.Z);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Positions, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	JsonAccessor.MinMaxLength = 3;
	JsonAccessor.Min[0] = MinPosition.X;
	JsonAccessor.Min[1] = MinPosition.Y;
	JsonAccessor.Min[2] = MinPosition.Z;
	JsonAccessor.Max[0] = MaxPosition.X;
	JsonAccessor.Max[1] = MaxPosition.Y;
	JsonAccessor.Max[2] = MaxPosition.Z;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFColorVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FColorVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFPackedColor> Colors;
	Colors.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Colors[VertexIndex] = FGLTFConverterUtility::ConvertColor(VertexBuffer->VertexColor(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Colors, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshNormalVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector3> Normals;
	Normals.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Normals[VertexIndex] = FGLTFConverterUtility::ConvertNormal(VertexBuffer->VertexTangentZ(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Normals, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshTangentVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector4> Tangents;
	Tangents.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Tangents[VertexIndex] = FGLTFConverterUtility::ConvertTangent(VertexBuffer->VertexTangentX(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Tangents, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshUVVertexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, TTuple<const FStaticMeshVertexBuffer*, int32> Params)
{
	const FStaticMeshVertexBuffer* VertexBuffer = Params.Get<0>();
	const int32 UVIndex = Params.Get<1>();

	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0 || UVIndex < 0 || VertexBuffer->GetNumTexCoords() <= static_cast<uint32>(UVIndex))
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FGLTFJsonVector2> UVs;
	UVs.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		UVs[VertexIndex] = FGLTFConverterUtility::ConvertUV(VertexBuffer->GetVertexUV(VertexIndex, UVIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(UVs, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonBufferViewIndex FGLTFStaticMeshIndexBufferConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FRawStaticIndexBuffer* IndexBuffer)
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

FGLTFJsonAccessorIndex FGLTFStaticMeshSectionConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, TTuple<const FStaticMeshSection*, const FRawStaticIndexBuffer*> Params)
{
	const FStaticMeshSection* MeshSection = Params.Get<0>();
	const FRawStaticIndexBuffer* IndexBuffer = Params.Get<1>();

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

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, TTuple<const UStaticMesh*, int32, const FColorVertexBuffer*> Params)
{
	const UStaticMesh* StaticMesh = Params.Get<0>();
	const int32 LODIndex = Params.Get<1>();
	const FColorVertexBuffer* OverrideVertexColors = Params.Get<2>();

	if (LODIndex < 0 || StaticMesh->GetNumLODs() <= LODIndex)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const int32 PrimaryUVIndex = 0; // TODO: make this configurable?
	const int32 LightmapUVIndex = StaticMesh->LightMapCoordinateIndex != PrimaryUVIndex ? StaticMesh->LightMapCoordinateIndex : INDEX_NONE;
	const FStaticMeshLODResources& LODResources = StaticMesh->GetLODForExport(LODIndex);

	FGLTFJsonMesh JsonMesh;
	JsonMesh.Name = Name;

	const FPositionVertexBuffer* PositionBuffer = &LODResources.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &LODResources.VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = OverrideVertexColors != nullptr ? OverrideVertexColors : &LODResources.VertexBuffers.ColorVertexBuffer;

	FGLTFJsonAttributes JsonAttributes;
	JsonAttributes.Position = Builder.GetOrAddPositionAccessor(PositionBuffer, Name + TEXT("_Positions"));
	JsonAttributes.Color0 = Builder.GetOrAddColorAccessor(ColorBuffer, Name + TEXT("_Colors"));
	JsonAttributes.Normal = Builder.GetOrAddNormalAccessor(VertexBuffer, Name + TEXT("_Normals"));
	JsonAttributes.Tangent = Builder.GetOrAddTangentAccessor(VertexBuffer, Name + TEXT("_Tangents"));
	JsonAttributes.TexCoord0 = Builder.GetOrAddUVAccessor(VertexBuffer, PrimaryUVIndex, Name + TEXT("_UV") + FString::FromInt(PrimaryUVIndex) + TEXT("s"));
	JsonAttributes.TexCoord1 = Builder.GetOrAddUVAccessor(VertexBuffer, LightmapUVIndex, Name + TEXT("_UV") + FString::FromInt(LightmapUVIndex) + TEXT("s"));

	const FRawStaticIndexBuffer* IndexBuffer = &LODResources.IndexBuffer;
	Builder.GetOrAddIndexBufferView(IndexBuffer, Name + TEXT("_Indices"));

	const int32 SectionCount = LODResources.Sections.Num();
	JsonMesh.Primitives.AddDefaulted(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[SectionIndex];
		JsonPrimitive.Attributes = JsonAttributes;

		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(&LODResources.Sections[SectionIndex], IndexBuffer,
            Name + (SectionCount != 1 ? TEXT("_Indices_Section") + FString::FromInt(SectionIndex) : TEXT("_Indices")));
	}

	return Builder.AddMesh(JsonMesh);
}
