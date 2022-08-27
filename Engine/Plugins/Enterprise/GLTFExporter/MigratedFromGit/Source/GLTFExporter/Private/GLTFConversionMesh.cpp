// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFConversionMesh.h"
#include "GLTFConversionUtilities.h"

FGLTFConversionSection::FGLTFConversionSection(const FString& SectionName, const FStaticMeshLODResources& LODMesh, int32 SectionIndex)
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

FGLTFJsonAccessorIndex FGLTFConversionSection::AppendAccessorForIndices(FGLTFContainer& Container) const
{
	if (Indices.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Indices");
	FGLTFJsonBufferViewIndex BufferViewIndex = Container.AppendBufferView(Indices, AttributeName, EGLTFJsonBufferTarget::ElementArrayBuffer);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U32;
	Accessor.Count = Indices.Num();
	Accessor.Type = EGLTFJsonAccessorType::Scalar;

	return Container.JsonRoot.Accessors.Add(Accessor);
}

FGLTFConversionMesh::FGLTFConversionMesh(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	StaticMesh->GetName(Name);

	const FStaticMeshLODResources& LODMesh = StaticMesh->GetLODForExport(LODIndex);

	const int32 SectionCount = LODMesh.Sections.Num();
	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FString SectionName = Name + TEXT("_Section") + FString::FromInt(SectionIndex);
		Sections.Add(FGLTFConversionSection(SectionName, LODMesh, SectionIndex));
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

FGLTFJsonAccessorIndex FGLTFConversionMesh::AppendAccessorForPositions(FGLTFContainer& Container) const
{
	if (Positions.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Positions");
	FGLTFJsonBufferViewIndex BufferViewIndex = Container.AppendBufferView(Positions, AttributeName);

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

	return Container.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConversionMesh::AppendAccessorForNormals(FGLTFContainer& Container) const
{
	if (Normals.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Normals");
	FGLTFJsonBufferViewIndex BufferViewIndex = Container.AppendBufferView(Normals, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Normals.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec3;

	return Container.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConversionMesh::AppendAccessorForColors(FGLTFContainer& Container) const
{
	if (Colors.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Colors");
	FGLTFJsonBufferViewIndex BufferViewIndex = Container.AppendBufferView(Colors, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::U8;
	Accessor.Count = Colors.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	return Container.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConversionMesh::AppendAccessorForTangents(FGLTFContainer& Container) const
{
	if (Tangents.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_Tangents");
	FGLTFJsonBufferViewIndex BufferViewIndex = Container.AppendBufferView(Tangents, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = Tangents.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec4;

	return Container.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConversionMesh::AppendAccessorForUV0s(FGLTFContainer& Container) const
{
	if (UV0s.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_UV0s");
	FGLTFJsonBufferViewIndex BufferViewIndex = Container.AppendBufferView(UV0s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV0s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	return Container.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonAccessorIndex FGLTFConversionMesh::AppendAccessorForUV1s(FGLTFContainer& Container) const
{
	if (UV1s.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FString AttributeName = Name + TEXT("_UV1s");
	FGLTFJsonBufferViewIndex BufferViewIndex = Container.AppendBufferView(UV1s, AttributeName);

	FGLTFJsonAccessor Accessor;
	Accessor.Name = AttributeName;
	Accessor.BufferView = BufferViewIndex;
	Accessor.ComponentType = EGLTFJsonComponentType::F32;
	Accessor.Count = UV1s.Num();
	Accessor.Type = EGLTFJsonAccessorType::Vec2;

	return Container.JsonRoot.Accessors.Add(Accessor);
}

FGLTFJsonMeshIndex FGLTFConversionMesh::AppendMesh(FGLTFContainer& Container) const
{
	FGLTFJsonMesh Mesh;
	Mesh.Name = Name;

	FGLTFJsonAttributes Attributes;
	Attributes.Position = AppendAccessorForPositions(Container);
	Attributes.Color0 = AppendAccessorForColors(Container);
	Attributes.Normal = AppendAccessorForNormals(Container);
	Attributes.Tangent = AppendAccessorForTangents(Container);
	Attributes.TexCoord0 = AppendAccessorForUV0s(Container);
	Attributes.TexCoord1 = AppendAccessorForUV1s(Container);

	for (const FGLTFConversionSection& Section : Sections)
	{
		FGLTFJsonPrimitive Primitive;
		Primitive.Attributes = Attributes;

		Primitive.Indices = Section.AppendAccessorForIndices(Container);
		Mesh.Primitives.Add(Primitive);
	}

	return Container.JsonRoot.Meshes.Add(Mesh);
}
