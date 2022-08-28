// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshConverters.h"
#include "GLTFContainerBuilder.h"
#include "GLTFConverterUtility.h"

FGLTFJsonAccessorIndex FGLTFPositionVertexBufferConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FPositionVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FVector> Positions;
	Positions.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Positions[VertexIndex] = FGLTFConverterUtility::ConvertPosition(VertexBuffer->VertexPosition(VertexIndex));
	}

	FBox BoundingBox;
	BoundingBox.Init();

	// More accurate bounding box if based on raw vertex values
	for (const FVector& Position : Positions)
	{
		BoundingBox += Position;
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Positions, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	JsonAccessor.MinMaxLength = 3;
	JsonAccessor.Max[0] = BoundingBox.Max.X;
	JsonAccessor.Max[1] = BoundingBox.Max.Y;
	JsonAccessor.Max[2] = BoundingBox.Max.Z;
	JsonAccessor.Min[0] = BoundingBox.Min.X;
	JsonAccessor.Min[1] = BoundingBox.Min.Y;
	JsonAccessor.Min[2] = BoundingBox.Min.Z;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFColorVertexBufferConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FColorVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FColor> Colors;
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

FGLTFJsonAccessorIndex FGLTFStaticMeshNormalVertexBufferConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FVector> Normals;
	Normals.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Normals[VertexIndex] = FGLTFConverterUtility::ConvertVector(VertexBuffer->VertexTangentZ(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Normals, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshTangentVertexBufferConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FVector4> Tangents;
	Tangents.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		Tangents[VertexIndex] = FGLTFConverterUtility::ConvertVector(VertexBuffer->VertexTangentX(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(Tangents, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshUV0VertexBufferConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0 || VertexBuffer->GetNumTexCoords() < 1)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FVector2D> UV0s;
	UV0s.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		UV0s[VertexIndex] = VertexBuffer->GetVertexUV(VertexIndex, 0);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(UV0s, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshUV1VertexBufferConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
{
	const uint32 VertexCount = VertexBuffer->GetNumVertices();
	if (VertexCount == 0 || VertexBuffer->GetNumTexCoords() < 2)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	TArray<FVector2D> UV1s;
	UV1s.AddUninitialized(VertexCount);

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		UV1s[VertexIndex] = VertexBuffer->GetVertexUV(VertexIndex, 1);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.AddBufferView(UV1s, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonBufferViewIndex FGLTFStaticMeshIndexBufferConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FRawStaticIndexBuffer* IndexBuffer)
{
	if (IndexBuffer->GetNumIndices() == 0)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	TArray<uint32> Indices;
	IndexBuffer->GetCopy(Indices);
	return Builder.AddBufferView(Indices, Name, EGLTFJsonBufferTarget::ElementArrayBuffer);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshSectionConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer)
{
	const uint32 TriangleCount = MeshSection->NumTriangles;
	if (TriangleCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Builder.GetOrAddIndexBufferView(IndexBuffer);
	JsonAccessor.ByteOffset = MeshSection->FirstIndex * sizeof(uint32);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U32;
	JsonAccessor.Count = TriangleCount * 3;
	JsonAccessor.Type = EGLTFJsonAccessorType::Scalar;

	return Builder.AddAccessor(JsonAccessor);
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Add(FGLTFIndexedBuilder& Builder, const FString& Name, const FStaticMeshLODResources* StaticMeshLOD, const FColorVertexBuffer* OverrideVertexColors)
{
	FGLTFJsonMesh JsonMesh;
	JsonMesh.Name = Name;

	const FPositionVertexBuffer* PositionBuffer = &StaticMeshLOD->VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &StaticMeshLOD->VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = OverrideVertexColors != nullptr ? OverrideVertexColors : &StaticMeshLOD->VertexBuffers.ColorVertexBuffer;

	FGLTFJsonAttributes JsonAttributes;
	JsonAttributes.Position = Builder.GetOrAddPositionAccessor(PositionBuffer, Name.IsEmpty() ? Name : Name + TEXT("_Positions"));
	JsonAttributes.Normal = Builder.GetOrAddNormalAccessor(VertexBuffer, Name.IsEmpty() ? Name : Name + TEXT("_Normals"));
	JsonAttributes.Tangent = Builder.GetOrAddTangentAccessor(VertexBuffer, Name.IsEmpty() ? Name : Name + TEXT("_Tangents"));
	JsonAttributes.TexCoord0 = Builder.GetOrAddUV0Accessor(VertexBuffer, Name.IsEmpty() ? Name : Name + TEXT("_UV0s"));
	JsonAttributes.TexCoord1 = Builder.GetOrAddUV1Accessor(VertexBuffer, Name.IsEmpty() ? Name : Name + TEXT("_UV1s"));
	JsonAttributes.Color0 = Builder.GetOrAddColorAccessor(ColorBuffer, Name.IsEmpty() ? Name : Name + TEXT("_Colors"));

	const FRawStaticIndexBuffer* IndexBuffer = &StaticMeshLOD->IndexBuffer;
	Builder.GetOrAddIndexBufferView(IndexBuffer, Name.IsEmpty() ? Name : Name + TEXT("_Indices"));

	const int32 SectionCount = StaticMeshLOD->Sections.Num();
	JsonMesh.Primitives.AddDefaulted(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[SectionIndex];
		JsonPrimitive.Attributes = JsonAttributes;

		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(&StaticMeshLOD->Sections[SectionIndex], IndexBuffer,
			Name.IsEmpty() ? Name : JsonMesh.Name + (SectionCount != 1 ? TEXT("_Indices_Section") + FString::FromInt(SectionIndex) : TEXT("_Indices")));
	}

	return Builder.AddMesh(JsonMesh);
}
