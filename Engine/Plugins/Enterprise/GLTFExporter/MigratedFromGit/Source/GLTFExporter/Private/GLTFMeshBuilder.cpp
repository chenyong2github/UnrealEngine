// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMeshBuilder.h"
#include "GLTFConverterUtility.h"

FGLTFAttributesBuilder::FGLTFAttributesBuilder(const FString& Name, const FStaticMeshLODResources& LODMesh, const FColorVertexBuffer* OverrideVertexColors)
	: Name(Name)
{
	const FPositionVertexBuffer& PositionVertexBuffer = LODMesh.VertexBuffers.PositionVertexBuffer;
	const int32 PositionCount = PositionVertexBuffer.GetNumVertices();

	Positions.AddUninitialized(PositionCount);

	for (int32 PosIndex = 0; PosIndex < PositionCount; ++PosIndex)
	{
		Positions[PosIndex] = FGLTFConverterUtility::ConvertPosition(PositionVertexBuffer.VertexPosition(PosIndex));
	}

	BoundingBox.Init();

	// Avoid glTF validation issue by using more accurate bounding box (based on raw vertex values)
	for (const FVector& Position : Positions)
	{
		BoundingBox += Position;
	}

	const FColorVertexBuffer& ColorVertexBuffer = OverrideVertexColors != nullptr ? *OverrideVertexColors : LODMesh.VertexBuffers.ColorVertexBuffer;
	const int32 ColorCount = ColorVertexBuffer.GetNumVertices();

	if (ColorCount > 0)
	{
		Colors.AddUninitialized(ColorCount);

		for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
		{
			Colors[ColorIndex] = FGLTFConverterUtility::ConvertColor(ColorVertexBuffer.VertexColor(ColorIndex));
		}
	}

	const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LODMesh.VertexBuffers.StaticMeshVertexBuffer;
	const int32 VertexCount = StaticMeshVertexBuffer.GetNumVertices();

	Normals.AddUninitialized(VertexCount);
	Tangents.AddUninitialized(VertexCount);

	for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
	{
		Normals[VertIndex] = FGLTFConverterUtility::ConvertVector(StaticMeshVertexBuffer.VertexTangentZ(VertIndex));
		Tangents[VertIndex] = FGLTFConverterUtility::ConvertTangent(StaticMeshVertexBuffer.VertexTangentX(VertIndex));
	}

	const int32 UVCount = StaticMeshVertexBuffer.GetNumTexCoords();
	if (UVCount >= 1)
	{
		UV0s.AddUninitialized(VertexCount);

		for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
		{
			UV0s[VertIndex] = StaticMeshVertexBuffer.GetVertexUV(VertIndex, 0);
		}

		if (UVCount >= 2)
		{
			UV1s.AddUninitialized(VertexCount);

			for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
			{
				UV1s[VertIndex] = StaticMeshVertexBuffer.GetVertexUV(VertIndex, 1);
			}
		}
	}
}

FGLTFJsonAccessorIndex FGLTFAttributesBuilder::AddPositions(FGLTFContainerBuilder& Container) const
{
	if (Positions.Num() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const FString AttributeName = Name + TEXT("_Positions");
	const FGLTFJsonBufferViewIndex BufferViewIndex = Container.AddBufferView(Positions, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Positions.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec3;
	
	JsonAccessor.MinMaxLength = 3;
	JsonAccessor.Max[0] = BoundingBox.Max.X;
	JsonAccessor.Max[1] = BoundingBox.Max.Y;
	JsonAccessor.Max[2] = BoundingBox.Max.Z;
	JsonAccessor.Min[0] = BoundingBox.Min.X;
	JsonAccessor.Min[1] = BoundingBox.Min.Y;
	JsonAccessor.Min[2] = BoundingBox.Min.Z;
	
	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFAttributesBuilder::AddNormals(FGLTFContainerBuilder& Container) const
{
	if (Normals.Num() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const FString AttributeName = Name + TEXT("_Normals");
	const FGLTFJsonBufferViewIndex BufferViewIndex = Container.AddBufferView(Normals, AttributeName);

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = AttributeName;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = Normals.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec3;
	
	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFAttributesBuilder::AddColors(FGLTFContainerBuilder& Container) const
{
	if (Colors.Num() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const FString AttributeName = Name + TEXT("_Colors");
	const FGLTFJsonBufferViewIndex BufferViewIndex = Container.AddBufferView(Colors, AttributeName);

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = AttributeName;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U8;
	JsonAccessor.Count = Colors.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;
	JsonAccessor.bNormalized = true;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFAttributesBuilder::AddTangents(FGLTFContainerBuilder& Container) const
{
	if (Tangents.Num() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const FString AttributeName = Name + TEXT("_Tangents");
	const FGLTFJsonBufferViewIndex BufferViewIndex = Container.AddBufferView(Tangents, AttributeName);

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = AttributeName;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = Tangents.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec4;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFAttributesBuilder::AddUV0s(FGLTFContainerBuilder& Container) const
{
	if (UV0s.Num() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const FString AttributeName = Name + TEXT("_UV0s");
	const FGLTFJsonBufferViewIndex BufferViewIndex = Container.AddBufferView(UV0s, AttributeName);

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = AttributeName;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = UV0s.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAccessorIndex FGLTFAttributesBuilder::AddUV1s(FGLTFContainerBuilder& Container) const
{
	if (UV1s.Num() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const FString AttributeName = Name + TEXT("_UV1s");
	const FGLTFJsonBufferViewIndex BufferViewIndex = Container.AddBufferView(UV1s, AttributeName);

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = AttributeName;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = EGLTFJsonComponentType::F32;
	JsonAccessor.Count = UV1s.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Vec2;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonAttributes FGLTFAttributesBuilder::AddAttributes(FGLTFContainerBuilder& Container) const
{
	FGLTFJsonAttributes JsonAttributes;
	JsonAttributes.Position = AddPositions(Container);
	JsonAttributes.Color0 = AddColors(Container);
	JsonAttributes.Normal = AddNormals(Container);
	JsonAttributes.Tangent = AddTangents(Container);
	JsonAttributes.TexCoord0 = AddUV0s(Container);
	JsonAttributes.TexCoord1 = AddUV1s(Container);
	return JsonAttributes;
}

FGLTFPrimitiveBuilder::FGLTFPrimitiveBuilder(const FString& SectionName, const FStaticMeshSection& MeshSection, const FIndexArrayView& IndexArray)
	: Name(SectionName)
{
	const uint32 IndexCount = MeshSection.NumTriangles * 3;
	const uint32 FirstIndex = MeshSection.FirstIndex;

	Indices.AddUninitialized(IndexCount);

	for (uint32 Index = 0; Index < IndexCount; ++Index)
	{
		Indices[Index] = IndexArray[FirstIndex + Index];
	}
}

FGLTFJsonAccessorIndex FGLTFPrimitiveBuilder::AddIndices(FGLTFContainerBuilder& Container) const
{
	if (Indices.Num() == 0)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	const FString AttributeName = Name + TEXT("_Indices");
	const FGLTFJsonBufferViewIndex BufferViewIndex = Container.AddBufferView(Indices, AttributeName, EGLTFJsonBufferTarget::ElementArrayBuffer);

	FGLTFJsonAccessor JsonAccessor;
	JsonAccessor.Name = AttributeName;
	JsonAccessor.BufferView = BufferViewIndex;
	JsonAccessor.ComponentType = EGLTFJsonComponentType::U32;
	JsonAccessor.Count = Indices.Num();
	JsonAccessor.Type = EGLTFJsonAccessorType::Scalar;

	return Container.AddAccessor(JsonAccessor);
}

FGLTFJsonPrimitive FGLTFPrimitiveBuilder::AddPrimitive(FGLTFContainerBuilder& Container, const FGLTFJsonAttributes& JsonAttributes) const
{
	FGLTFJsonPrimitive JsonPrimitive;
	JsonPrimitive.Indices = AddIndices(Container);
	JsonPrimitive.Attributes = JsonAttributes;
	return JsonPrimitive;
}

FGLTFMeshBuilder::FGLTFMeshBuilder(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors)
	: FGLTFMeshBuilder(StaticMesh->GetName() + (LODIndex != 0 ? TEXT("_LOD") + FString::FromInt(LODIndex) : FString()), StaticMesh->GetLODForExport(LODIndex), OverrideVertexColors)
{
}

FGLTFMeshBuilder::FGLTFMeshBuilder(const FString& Name, const FStaticMeshLODResources& LODMesh, const FColorVertexBuffer* OverrideVertexColors)
	: Attributes(Name, LODMesh, OverrideVertexColors)
{
	const FIndexArrayView IndexArray = LODMesh.IndexBuffer.GetArrayView();
	const int32 SectionCount = LODMesh.Sections.Num();

	Primitives.Reserve(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FString SectionName = Name + TEXT("_Section") + FString::FromInt(SectionIndex);
		Primitives.Emplace(SectionName, LODMesh.Sections[SectionIndex], IndexArray);
	}
}

FGLTFJsonMeshIndex FGLTFMeshBuilder::AddMesh(FGLTFContainerBuilder& Container) const
{
	const FGLTFJsonAttributes JsonAttributes = Attributes.AddAttributes(Container);
	
	FGLTFJsonMesh Mesh;
	Mesh.Name = Name;

	for (const FGLTFPrimitiveBuilder& Primitive : Primitives)
	{
		Mesh.Primitives.Add(Primitive.AddPrimitive(Container, JsonAttributes));
	}

	return Container.AddMesh(Mesh);
}
