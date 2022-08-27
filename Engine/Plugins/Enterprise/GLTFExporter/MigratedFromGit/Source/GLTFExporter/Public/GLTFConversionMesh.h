// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "GLTFContainer.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFConversionSection
{
	FString Name;

	TArray<uint32> Indices;

	FGLTFConversionSection(const FString& SectionName, const FStaticMeshLODResources& LODMesh, int32 SectionIndex);

	FGLTFJsonAccessorIndex AppendAccessorForIndices(FGLTFContainer& Container) const;
};

struct GLTFEXPORTER_API FGLTFConversionMesh
{
	FString Name;

	TArray<FGLTFConversionSection> Sections;

	TArray<FVector>   Positions;
	TArray<FColor>    Colors;
	TArray<FVector>   Normals;
	TArray<FVector4>  Tangents;
	TArray<FVector2D> UV0s;
	TArray<FVector2D> UV1s;

	FBox BoundingBox;

	FGLTFConversionMesh(const UStaticMesh* StaticMesh, int32 LODIndex);

	FGLTFJsonAccessorIndex AppendAccessorForPositions(FGLTFContainer& Container) const;
	FGLTFJsonAccessorIndex AppendAccessorForColors(FGLTFContainer& Container) const;
	FGLTFJsonAccessorIndex AppendAccessorForNormals(FGLTFContainer& Container) const;
	FGLTFJsonAccessorIndex AppendAccessorForTangents(FGLTFContainer& Container) const;
	FGLTFJsonAccessorIndex AppendAccessorForUV0s(FGLTFContainer& Container) const;
	FGLTFJsonAccessorIndex AppendAccessorForUV1s(FGLTFContainer& Container) const;

	FGLTFJsonMeshIndex AppendMesh(FGLTFContainer& Container) const;
};
