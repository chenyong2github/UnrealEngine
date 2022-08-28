// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "GLTFContainerBuilder.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFSectionBuilder
{
	FString Name;

	TArray<uint32> Indices;

	FGLTFSectionBuilder(const FString& SectionName, const FStaticMeshSection& MeshSection, const FIndexArrayView& IndexArray);

	FGLTFJsonAccessorIndex AddAccessorForIndices(FGLTFContainerBuilder& Container) const;
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

	FGLTFJsonAccessorIndex AddAccessorForPositions(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddAccessorForColors(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddAccessorForNormals(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddAccessorForTangents(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddAccessorForUV0s(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddAccessorForUV1s(FGLTFContainerBuilder& Container) const;

	FGLTFJsonMeshIndex AddMesh(FGLTFContainerBuilder& Container) const;
};
