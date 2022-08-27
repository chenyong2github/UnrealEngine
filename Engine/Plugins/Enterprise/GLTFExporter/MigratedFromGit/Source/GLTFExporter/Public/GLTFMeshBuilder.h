// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "GLTFContainerBuilder.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFAttributesBuilder
{
	FString Name;

	FBox BoundingBox;

	TArray<FVector>   Positions;
	TArray<FColor>    Colors;
	TArray<FVector>   Normals;
	TArray<FVector4>  Tangents;
	TArray<FVector2D> UV0s;
	TArray<FVector2D> UV1s;

	FGLTFAttributesBuilder(const FString& Name, const FStaticMeshLODResources& LODMesh, const FColorVertexBuffer* OverrideVertexColors);

	FGLTFJsonAccessorIndex AddPositions(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddColors(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddNormals(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddTangents(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddUV0s(FGLTFContainerBuilder& Container) const;
	FGLTFJsonAccessorIndex AddUV1s(FGLTFContainerBuilder& Container) const;

	FGLTFJsonAttributes AddAttributes(FGLTFContainerBuilder& Container) const;
};

struct GLTFEXPORTER_API FGLTFPrimitiveBuilder
{
	FString Name;

	TArray<uint32> Indices;

	FGLTFPrimitiveBuilder(const FString& Name, const FStaticMeshSection& MeshSection, const FIndexArrayView& IndexArray);

	FGLTFJsonAccessorIndex AddIndices(FGLTFContainerBuilder& Container) const;

	FGLTFJsonPrimitive AddPrimitive(FGLTFContainerBuilder& Container, const FGLTFJsonAttributes& JsonAttributes) const;
};

struct GLTFEXPORTER_API FGLTFMeshBuilder
{
	FString Name;
	
	FGLTFAttributesBuilder Attributes;
	TArray<FGLTFPrimitiveBuilder> Primitives;

	FGLTFMeshBuilder(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors);
	FGLTFMeshBuilder(const FString& Name, const FStaticMeshLODResources& LODMesh, const FColorVertexBuffer* OverrideVertexColors);

	FGLTFJsonMeshIndex AddMesh(FGLTFContainerBuilder& Container) const;
};
