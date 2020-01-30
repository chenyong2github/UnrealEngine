// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Engine/World.h"

class UStaticMesh;
class USkeletalMesh;
class UGeometryCollection;

typedef TTuple<const UStaticMesh *, const UStaticMeshComponent *, FTransform> GeometryCollectionStaticMeshConversionTuple;
typedef TTuple<const USkeletalMesh *, const USkeletalMeshComponent *, FTransform> GeometryCollectionSkeletalMeshConversionTuple;

/**
* The public interface to this module
*/
class GEOMETRYCOLLECTIONEDITOR_API FGeometryCollectionConversion
{
public:

	/**
	*  Appends a static mesh to a GeometryCollectionComponent.
	*  @param StaticMeshComponent : Const mesh to read vertex/normals/index data from
	*  @param StaticMeshTransform : Mesh transform.
	*  @param GeometryCollection  : Collection to append the mesh into.
	*/
	static void AppendStaticMesh(const UStaticMesh * StaticMesh, const UStaticMeshComponent *StaticMeshComponent, const FTransform & StaticMeshTransform, UGeometryCollection * GeometryCollection, bool ReindexMaterials = true);

	/**
	*  Appends a skeletal mesh to a GeometryCollectionComponent.
	*  @param SkeletalMeshComponent : Const mesh to read vertex/normals/index data from
	*  @param SkeletalMeshTransform : Mesh transform.
	*  @param GeometryCollection    : Collection to append the mesh into.
	*/
	static void AppendSkeletalMesh(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent *SkeletalMeshComponent, const FTransform & SkeletalMeshTransform, UGeometryCollection * GeometryCollection, bool ReindexMaterials = true);

	/**
	*  Command invoked from "GeometryCollection.CreatGeometryCollection", uses the selected Actors to create a GeometryCollection Asset
	*  @param World
	*/
	static void CreateGeometryCollectionCommand(UWorld * World);

	/**
	*  Command invoked from "GeometryCollection.CreateFromSelectedActors", uses the selected Actors to create a GeometryCollection Asset
	*  @param World
	*/
	static void CreateFromSelectedActorsCommand(UWorld * World);

	/**
	*  Command invoked from "GeometryCollection.CreateFromSelectedAssets", uses the selected Assets to create a GeometryCollection Asset
	*  @param World
	*/
	static void CreateFromSelectedAssetsCommand(UWorld * World);
};