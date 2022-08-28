// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMeshBuilder.h"
#include "GLTFConversionUtilities.h"

FGLTFSectionBuilder::FGLTFSectionBuilder(const FString& SectionName, const FStaticMeshSection& MeshSection, const FIndexArrayView& IndexArray)
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

FGLTFJsonAccessorIndex FGLTFSectionBuilder::AddAccessorForIndices(FGLTFBuilder& Builder) const
{
	if (Indices.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Indices");
	FGLTFJsonBufferViewIndex BufferViewIndex = Builder.AddBufferView(Indices, AttributeName, EGLTFJsonBufferTarget::ElementArrayBuffer);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U32;
	Accessor.Count = Indices.Num();
	Accessor.Type = EGLTFJsonAccessorType::Scalar;

	return Builder.JsonRoot.Accessors.Add(Accessor);
}

FGLTFMeshBuilder::FGLTFMeshBuilder(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	StaticMesh->GetName(Name);

	const FStaticMeshLODResources& LODMesh = StaticMesh->GetLODForExport(LODIndex);

	const FIndexArrayView IndexArray = LODMesh.IndexBuffer.GetArrayView();
	const int32 SectionCount = LODMesh.Sections.Num();

	Sections.Reserve(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FString SectionName = Name + TEXT("_Section") + FString::FromInt(SectionIndex);
		Sections.Emplace(SectionName, LODMesh.Sections[SectionIndex], IndexArray);
	}

	const FPositionVertexBuffer& PositionVertexBuffer = LODMesh.VertexBuffers.PositionVertexBuffer;
	const int32 PositionCount = PositionVertexBuffer.GetNumVertices();

	Positions.AddUninitialized(PositionCount);

	for (int32 PosIndex = 0; PosIndex < PositionCount; ++PosIndex)
	{
		Positions[PosIndex] = ConvertPosition(PositionVertexBuffer.VertexPosition(PosIndex));
	}

	const FColorVertexBuffer& ColorVertexBuffer = LODMesh.VertexBuffers.ColorVertexBuffer;
	const int32 ColorCount = ColorVertexBuffer.GetNumVertices();

	if (ColorCount > 0)
	{
		Colors.AddUninitialized(ColorCount);

		for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
		{
			Colors[ColorIndex] = ConvertColor(ColorVertexBuffer.VertexColor(ColorIndex));
		}
	}

	const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LODMesh.VertexBuffers.StaticMeshVertexBuffer;
	const int32 VertexCount = StaticMeshVertexBuffer.GetNumVertices();

	Normals.AddUninitialized(VertexCount);
	Tangents.AddUninitialized(VertexCount);

	for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
	{
		Normals[VertIndex] = ConvertVector(StaticMeshVertexBuffer.VertexTangentZ(VertIndex));
		Tangents[VertIndex] = ConvertTangent(StaticMeshVertexBuffer.VertexTangentX(VertIndex));
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

	BoundingBox = StaticMesh->GetBoundingBox();
}

FGLTFJsonAccessorIndex FGLTFMeshBuilder::AddAccessorForPositions(FGLTFBuilder& Builder) const
{
	if (Positions.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Positions");
	FGLTFJsonBufferViewIndex BufferViewIndex = Builder.AddBufferView(Positions, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Positions.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec3;

	FVector Max = ConvertSize(BoundingBox.Max);
	FVector Min = ConvertSize(BoundingBox.Min);
	Accessor.Max[0] = Max.X;
	Accessor.Max[1] = Max.Y;
	Accessor.Max[2] = Max.Z;
	Accessor.Min[0] = Min.X;
	Accessor.Min[1] = Min.Y;
	Accessor.Min[2] = Min.Z;
	Accessor.MinMaxLength = 3;

	return Builder.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFMeshBuilder::AddAccessorForNormals(FGLTFBuilder& Builder) const
{
	if (Normals.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Normals");
	FGLTFJsonBufferViewIndex BufferViewIndex = Builder.AddBufferView(Normals, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Normals.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec3;

	return Builder.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFMeshBuilder::AddAccessorForColors(FGLTFBuilder& Builder) const
{
	if (Colors.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Colors");
	FGLTFJsonBufferViewIndex BufferViewIndex = Builder.AddBufferView(Colors, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U8;
	Accessor.Count = Colors.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFMeshBuilder::AddAccessorForTangents(FGLTFBuilder& Builder) const
{
	if (Tangents.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Tangents");
	FGLTFJsonBufferViewIndex BufferViewIndex = Builder.AddBufferView(Tangents, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Tangents.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	return Builder.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFMeshBuilder::AddAccessorForUV0s(FGLTFBuilder& Builder) const
{
	if (UV0s.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_UV0s");
	FGLTFJsonBufferViewIndex BufferViewIndex = Builder.AddBufferView(UV0s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV0s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFMeshBuilder::AddAccessorForUV1s(FGLTFBuilder& Builder) const
{
	if (UV1s.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_UV1s");
	FGLTFJsonBufferViewIndex BufferViewIndex = Builder.AddBufferView(UV1s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV1s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	return Builder.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonMeshIndex FGLTFMeshBuilder::AddMesh(FGLTFBuilder& Builder) const
{
	FGLTFJsonMesh Mesh;
	Mesh.Name = Name;

	FGLTFJsonAttributes Attributes;
	Attributes.Position = AddAccessorForPositions(Builder);
	Attributes.Color0 = AddAccessorForColors(Builder);
	Attributes.Normal = AddAccessorForNormals(Builder);
	Attributes.Tangent = AddAccessorForTangents(Builder);
	Attributes.TexCoord0 = AddAccessorForUV0s(Builder);
	Attributes.TexCoord1 = AddAccessorForUV1s(Builder);

	for (const FGLTFSectionBuilder& Section : Sections)
	{
		FGLTFJsonPrimitive Primitive;
		Primitive.Attributes = Attributes;

		Primitive.Indices = Section.AddAccessorForIndices(Builder);
		Mesh.Primitives.Add(Primitive);
	}

	return Builder.JsonRoot.Meshes.Add(Mesh);
}
