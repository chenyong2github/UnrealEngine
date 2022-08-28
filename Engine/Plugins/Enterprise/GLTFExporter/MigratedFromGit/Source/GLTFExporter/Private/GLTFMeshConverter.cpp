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

FGLTFJsonAccessorIndex FGLTFConvertedSection::AppendAccessorForIndices(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Indices.Num() == 0)
	{
		return INDEX_NONE;
	}

	FString AttributeName = Name + TEXT("_Indices");
	FGLTFJsonBufferViewIndex BufferViewIndex = BufferBuilder.AppendBufferView(Indices, AttributeName, EGLTFJsonBufferTarget::ElementArrayBuffer);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U32;
	Accessor.Count = Indices.Num();
	Accessor.Type = EGLTFJsonAccessorType::Scalar;

	return Root.Accessors.Add(Accessor);
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

FGLTFJsonAccessorIndex FGLTFConvertedMesh::AppendAccessorForPositions(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Positions.Num() == 0)
	{
		return INDEX_NONE;
	}

	FString AttributeName = Name + TEXT("_Positions");
	FGLTFJsonBufferViewIndex BufferViewIndex = BufferBuilder.AppendBufferView(Positions, AttributeName);

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

	return Root.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConvertedMesh::AppendAccessorForNormals(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Normals.Num() == 0)
	{
		return INDEX_NONE;
	}

	FString AttributeName = Name + TEXT("_Normals");
	FGLTFJsonBufferViewIndex BufferViewIndex = BufferBuilder.AppendBufferView(Normals, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Normals.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec3;

	return Root.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConvertedMesh::AppendAccessorForColors(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Colors.Num() == 0)
	{
		return INDEX_NONE;
	}

	FString AttributeName = Name + TEXT("_Colors");
	FGLTFJsonBufferViewIndex BufferViewIndex = BufferBuilder.AppendBufferView(Colors, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U8;
	Accessor.Count = Colors.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	return Root.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConvertedMesh::AppendAccessorForTangents(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (Tangents.Num() == 0)
	{
		return INDEX_NONE;
	}

	FString AttributeName = Name + TEXT("_Tangents");
	FGLTFJsonBufferViewIndex BufferViewIndex = BufferBuilder.AppendBufferView(Tangents, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Tangents.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	return Root.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConvertedMesh::AppendAccessorForUV0s(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (UV0s.Num() == 0)
	{
		return INDEX_NONE;
	}

	FString AttributeName = Name + TEXT("_UV0s");
	FGLTFJsonBufferViewIndex BufferViewIndex = BufferBuilder.AppendBufferView(UV0s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV0s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	return Root.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConvertedMesh::AppendAccessorForUV1s(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
{
	if (UV1s.Num() == 0)
	{
		return INDEX_NONE;
	}

	FString AttributeName = Name + TEXT("_UV1s");
	FGLTFJsonBufferViewIndex BufferViewIndex = BufferBuilder.AppendBufferView(UV1s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV1s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	return Root.Accessors.Add(Accessor);
}

FGLTFJsonMeshIndex FGLTFConvertedMesh::AppendMesh(FGLTFJsonRoot& Root, FGLTFBufferBuilder& BufferBuilder) const
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

	return Root.Meshes.Add(Mesh);
}
