// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "GLTFBufferBuilder.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFConvertedSection
{
	FString Name;

	TArray<uint32> Indices;

	FGLTFConvertedSection(const FString& SectionName, const FStaticMeshLODResources& LODMesh, int32 SectionIndex);

	FGLTFJsonAccessorIndex AppendAccessorForIndices(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;
};

struct GLTFEXPORTER_API FGLTFConvertedMesh
{
	FString Name;

	TArray<FGLTFConvertedSection> Sections;

	TArray<FVector>   Positions;
	TArray<FColor>    Colors;
	TArray<FVector>   Normals;
	TArray<FVector4>  Tangents;
	TArray<FVector2D> UV0s;
	TArray<FVector2D> UV1s;

	FBox BoundingBox;

	FGLTFConvertedMesh(const UStaticMesh* StaticMesh, int32 LODIndex);

	FGLTFJsonAccessorIndex AppendAccessorForPositions(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;
	FGLTFJsonAccessorIndex AppendAccessorForColors(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;
	FGLTFJsonAccessorIndex AppendAccessorForNormals(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;
	FGLTFJsonAccessorIndex AppendAccessorForTangents(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;
	FGLTFJsonAccessorIndex AppendAccessorForUV0s(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;
	FGLTFJsonAccessorIndex AppendAccessorForUV1s(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;

	FGLTFJsonMeshIndex AppendMesh(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder) const;
};

struct GLTFEXPORTER_API FGLTFMeshConverter
{
	FGLTFJsonRoot& JsonRoot;
	FGLTFBufferBuilder& BufferBuilder;

	FGLTFMeshConverter(FGLTFJsonRoot& JsonRoot, FGLTFBufferBuilder& BufferBuilder)
		: JsonRoot(JsonRoot)
		, BufferBuilder(BufferBuilder)
	{
	}

	FGLTFJsonMeshIndex AppendMesh(const UStaticMesh* StaticMesh, int32 LODIndex)
	{
		return FGLTFConvertedMesh(StaticMesh, LODIndex).AppendMesh(JsonRoot, BufferBuilder);
	}
};
