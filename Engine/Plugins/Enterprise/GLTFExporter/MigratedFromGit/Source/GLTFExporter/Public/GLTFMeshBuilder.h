// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "GLTFBuilder.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFSectionBuilder
{
	FString Name;

	TArray<uint32> Indices;

	FGLTFSectionBuilder(const FString& SectionName, const FStaticMeshSection& MeshSection, const FIndexArrayView& IndexArray);

	FGLTFJsonAccessorIndex AppendAccessorForIndices(FGLTFBuilder& Builder) const;
};

struct GLTFEXPORTER_API FGLTFMeshBuilder
{
	FString Name;

	TArray<FGLTFSectionBuilder> Sections;

	TArray<FVector>   Positions;
	TArray<FColor>    Colors;
	TArray<FVector>   Normals;
	TArray<FVector4>  Tangents;
	TArray<FVector2D> UV0s;
	TArray<FVector2D> UV1s;

	FBox BoundingBox;

	FGLTFMeshBuilder(const UStaticMesh* StaticMesh, int32 LODIndex);

	FGLTFJsonAccessorIndex AppendAccessorForPositions(FGLTFBuilder& Builder) const;
	FGLTFJsonAccessorIndex AppendAccessorForColors(FGLTFBuilder& Builder) const;
	FGLTFJsonAccessorIndex AppendAccessorForNormals(FGLTFBuilder& Builder) const;
	FGLTFJsonAccessorIndex AppendAccessorForTangents(FGLTFBuilder& Builder) const;
	FGLTFJsonAccessorIndex AppendAccessorForUV0s(FGLTFBuilder& Builder) const;
	FGLTFJsonAccessorIndex AppendAccessorForUV1s(FGLTFBuilder& Builder) const;

	FGLTFJsonMeshIndex AppendMesh(FGLTFBuilder& Builder) const;
};
