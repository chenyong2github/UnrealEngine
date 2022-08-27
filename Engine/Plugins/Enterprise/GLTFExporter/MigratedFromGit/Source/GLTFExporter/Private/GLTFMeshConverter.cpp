// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMeshConverter.h"
#include "GLTFConversionUtilities.h"

FGLTFConvertedSection::FGLTFConvertedSection(const FString& SectionName, const FStaticMeshLODResources& LODMesh, int32 SectionIndex)
	: Name(SectionName)
{
	FIndexArrayView RawIndices = LODMesh.IndexBuffer.GetArrayView();
	const FStaticMeshSection& Section = LODMesh.Sections[SectionIndex];

	const uint32 IndexCount = Section.NumTriangles * 3;
	Indices.AddUninitialized(IndexCount);

	for (uint32 Index = 0; Index < IndexCount; ++Index)
	{
		Indices[Index] = RawIndices[Section.FirstIndex + Index];
	}
}

FGLTFJsonIndex FGLTFConvertedSection::AppendAccessorForIndices(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Indices.Num() == 0) return INDEX_NONE;

	FString AttributeName = Name + TEXT("_Indices");
	FGLTFJsonIndex BufferViewIndex = BufferBuilder.AppendBufferView(Indices, AttributeName, EGLTFJsonBufferTarget::ElementArrayBuffer);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U32;
	Accessor.Count = Indices.Num();
	Accessor.Type = EGLTFJsonAccessorType::Scalar;

	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);
	return AccessorIndex;
}

FGLTFConvertedMesh::FGLTFConvertedMesh(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	StaticMesh->GetName(Name);

	const FStaticMeshLODResources& LODMesh = StaticMesh->GetLODForExport(LODIndex);

	const int32 SectionCount = LODMesh.Sections.Num();
	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FString SectionName = Name + TEXT("_Section") + FString::FromInt(SectionIndex);
		Sections.Add(FGLTFConvertedSection(SectionName, LODMesh, SectionIndex));
	}

	const int32 PositionCount = LODMesh.VertexBuffers.PositionVertexBuffer.GetNumVertices();
	Positions.AddUninitialized(PositionCount);

	for (int32 PosIndex = 0; PosIndex < PositionCount; ++PosIndex)
	{
		Positions[PosIndex] = ConvertPosition(LODMesh.VertexBuffers.PositionVertexBuffer.VertexPosition(PosIndex));
	}

	const int32 ColorCount = LODMesh.VertexBuffers.ColorVertexBuffer.GetNumVertices();
	if (ColorCount > 0)
	{
		Colors.AddUninitialized(ColorCount);
		for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
		{
			Colors[ColorIndex] = ConvertColor(LODMesh.VertexBuffers.ColorVertexBuffer.VertexColor(ColorIndex));
		}
	}

	const int32 VertexCount = LODMesh.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
	Normals.AddUninitialized(VertexCount);
	Tangents.AddUninitialized(VertexCount);

	for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
	{
		Normals[VertIndex] = ConvertVector(LODMesh.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertIndex));
		Tangents[VertIndex] = ConvertTangent(LODMesh.VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertIndex));
	}

	const int32 UVCount = LODMesh.GetNumTexCoords();
	if (UVCount >= 1)
	{
		UV0s.AddUninitialized(VertexCount);
		for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
		{
			UV0s[VertIndex] = LODMesh.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertIndex, 0);
		}

		if (UVCount >= 2)
		{
			UV1s.AddUninitialized(VertexCount);
			for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
			{
				UV1s[VertIndex] = LODMesh.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertIndex, 1);
			}
		}
	}

	BoundingBox = StaticMesh->GetBoundingBox();
}

FGLTFJsonIndex FGLTFConvertedMesh::AppendAccessorForPositions(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Positions.Num() == 0) return INDEX_NONE;

	FString AttributeName = Name + TEXT("_Positions");
	FGLTFJsonIndex BufferViewIndex = BufferBuilder.AppendBufferView(Positions, AttributeName);

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

	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);
	return AccessorIndex;
}

FGLTFJsonIndex FGLTFConvertedMesh::AppendAccessorForNormals(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Normals.Num() == 0) return INDEX_NONE;

	FString AttributeName = Name + TEXT("_Normals");
	FGLTFJsonIndex BufferViewIndex = BufferBuilder.AppendBufferView(Normals, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Normals.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec3;
	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);

	return AccessorIndex;
}

FGLTFJsonIndex FGLTFConvertedMesh::AppendAccessorForColors(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Colors.Num() == 0) return INDEX_NONE;

	FString AttributeName = Name + TEXT("_Colors");
	FGLTFJsonIndex BufferViewIndex = BufferBuilder.AppendBufferView(Colors, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U8;
	Accessor.Count = Colors.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);
	return AccessorIndex;
}

FGLTFJsonIndex FGLTFConvertedMesh::AppendAccessorForTangents(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Tangents.Num() == 0) return INDEX_NONE;

	FString AttributeName = Name + TEXT("_Tangents");
	FGLTFJsonIndex BufferViewIndex = BufferBuilder.AppendBufferView(Tangents, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Tangents.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);
	return AccessorIndex;
}

FGLTFJsonIndex FGLTFConvertedMesh::AppendAccessorForUV0s(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (UV0s.Num() == 0) return INDEX_NONE;

	FString AttributeName = Name + TEXT("_UV0s");
	FGLTFJsonIndex BufferViewIndex = BufferBuilder.AppendBufferView(UV0s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV0s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);
	return AccessorIndex;
}

FGLTFJsonIndex FGLTFConvertedMesh::AppendAccessorForUV1s(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (UV1s.Num() == 0) return INDEX_NONE;

	FString AttributeName = Name + TEXT("_UV1s");
	FGLTFJsonIndex BufferViewIndex = BufferBuilder.AppendBufferView(UV1s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV1s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);
	return AccessorIndex;
}

FGLTFJsonIndex FGLTFConvertedMesh::AppendMesh(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	FGLTFJsonMesh Mesh;
	Mesh.Name = Name;

	FGLTFJsonAttributes Attributes;
	Attributes.Position = AppendAccessorForPositions(Root, BufferBuilder);
	Attributes.Color0 = AppendAccessorForColors(Root, BufferBuilder);
	Attributes.Normal = AppendAccessorForNormals(Root, BufferBuilder);
	Attributes.Tangent = AppendAccessorForTangents(Root, BufferBuilder);
	Attributes.TexCoord0 = AppendAccessorForUV0s(Root, BufferBuilder);
	Attributes.TexCoord1 = AppendAccessorForUV1s(Root, BufferBuilder);

	for (const FGLTFConvertedSection& Section : Sections)
	{
		FGLTFJsonPrimitive Primitive;
		Primitive.Attributes = Attributes;

		Primitive.Indices = Section.AppendAccessorForIndices(Root, BufferBuilder);
		Mesh.Primitives.Add(Primitive);
	}

	FGLTFJsonIndex MeshIndex = Root.Meshes.Add(Mesh);
	return MeshIndex;
}
