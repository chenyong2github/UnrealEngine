// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshConverters.h"
#include "GLTFContainerBuilder.h"
#include "GLTFConversionUtilities.h"

FGLTFJsonAccessorIndex FGLTFPositionVertexBufferConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FPositionVertexBuffer* VertexBuffer)
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
		Positions[VertexIndex] = ConvertPosition(VertexBuffer->VertexPosition(VertexIndex));
	}

	FBox BoundingBox;

	// More accurate bounding box if based on raw vertex values
	for (const FVector& Position : Positions)
	{
		BoundingBox += Position;
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Container.AddBufferView(Positions, Name);
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
	
	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFColorVertexBufferConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FColorVertexBuffer* VertexBuffer)
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
		Colors[VertexIndex] = ConvertColor(VertexBuffer->VertexColor(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Container.AddBufferView(Colors, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshNormalVertexBufferConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
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
		Normals[VertexIndex] = ConvertVector(VertexBuffer->VertexTangentZ(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Container.AddBufferView(Normals, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;
	
	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshTangentVertexBufferConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
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
		Tangents[VertexIndex] = ConvertVector(VertexBuffer->VertexTangentX(VertexIndex));
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Container.AddBufferView(Tangents, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	
	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshUV0VertexBufferConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
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
	JsonAccessor.BufferView = Container.AddBufferView(UV0s, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshUV1VertexBufferConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FStaticMeshVertexBuffer* VertexBuffer)
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
	JsonAccessor.BufferView = Container.AddBufferView(UV1s, Name);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = VertexCount;
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonBufferViewIndex FGLTFStaticMeshIndexBufferConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FRawStaticIndexBuffer* IndexBuffer)
{
	if (IndexBuffer->GetNumIndices() == 0)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	TArray<uint32> Indices;
	IndexBuffer->GetCopy(Indices);
	return Container.CreateBufferView(Indices, Name, EGLTFJsonBufferTarget::ElementArrayBuffer);
}

FGLTFJsonAccessorIndex FGLTFStaticMeshSectionConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer)
{	
	const uint32 TriangleCount = MeshSection->NumTriangles;
	if (TriangleCount == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = Name;
	JsonAccessor.BufferView = Container.AddIndexBufferView(IndexBuffer);
	JsonAccessor.ByteOffset = MeshSection->FirstIndex * sizeof(uint32);
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U32;
	JsonAccessor.Count = TriangleCount * 3;
	JsonAccessor.Type = EGLTFJsonAccessorType::Scalar;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors)
{
	const FStaticMeshLODResources& LODMesh = StaticMesh->GetLODForExport(LODIndex);
	const FPositionVertexBuffer* PositionBuffer = &LODMesh.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &LODMesh.VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = OverrideVertexColors != nullptr ? OverrideVertexColors : &LODMesh.VertexBuffers.ColorVertexBuffer;

	FGLTFJsonMesh JsonMesh;
	JsonMesh.Name = Name;

	if (JsonMesh.Name.IsEmpty())
	{
		StaticMesh->GetName(JsonMesh.Name);
		if (LODIndex != 0) JsonMesh.Name += TEXT("_LOD") + FString::FromInt(LODIndex);
	}

	FGLTFJsonAttributes JsonAttributes;
	JsonAttributes.Position = Container.ConvertPositionAccessor(PositionBuffer, Name + TEXT("_Positions"));
	JsonAttributes.Normal = Container.ConvertNormalAccessor(VertexBuffer, Name + TEXT("_Normals"));
	JsonAttributes.Tangent = Container.ConvertTangentAccessor(VertexBuffer, Name + TEXT("_Tangents"));
	JsonAttributes.TexCoord0 = Container.ConvertUV0Accessor(VertexBuffer, Name + TEXT("_UV0s"));
	JsonAttributes.TexCoord1 = Container.ConvertUV1Accessor(VertexBuffer, Name + TEXT("_UV1s"));
	JsonAttributes.Color0 = Container.ConvertColorAccessor(ColorBuffer, Name + TEXT("_Colors"));

	const FRawStaticIndexBuffer* IndexBuffer = &LODMesh.IndexBuffer;
	Container.ConvertIndexBufferView(IndexBuffer, Name + TEXT("_Indices"));

	const int32 SectionCount = LODMesh.Sections.Num();
	JsonMesh.Primitives.AddDefaulted(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[SectionIndex];
		JsonPrimitive.Attributes = JsonAttributes;

		const FString SectionName = JsonMesh.Name + (SectionCount != 0 ? TEXT("_Indices_Section") + FString::FromInt(SectionIndex) : TEXT("_Indices"));
		JsonPrimitive.Indices = Container.ConvertIndexAccessor(&LODMesh.Sections[SectionIndex], IndexBuffer, SectionName);
	}

	return Container.AddMesh(JsonMesh);
}
