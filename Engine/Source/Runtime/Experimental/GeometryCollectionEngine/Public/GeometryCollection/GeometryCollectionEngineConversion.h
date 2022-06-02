// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Engine/World.h"

class UStaticMesh;
class USkeletalMesh;
class UGeometryCollection;
class UMaterialInterface;
class UGeometryCollectionComponent;

typedef TTuple<const UStaticMesh *, const UStaticMeshComponent *, FTransform> GeometryCollectionStaticMeshConversionTuple;
typedef TTuple<const USkeletalMesh *, const USkeletalMeshComponent *, FTransform> GeometryCollectionSkeletalMeshConversionTuple;

/**
* The public interface to this module
*/
class GEOMETRYCOLLECTIONENGINE_API FGeometryCollectionEngineConversion
{
public:

	/**
	*  Appends a static mesh to a GeometryCollectionComponent.
	*  @param StaticMesh : Const mesh to read vertex/normals/index data from
	*  @param Materials : Materials fetched from the StaticMeshComponent used to configure this geometry
	*  @param StaticMeshTransform : Mesh transform.
	*  @param GeometryCollection  : Collection to append the mesh into.
	*/
	static void AppendStaticMesh(const UStaticMesh* StaticMesh, const TArray<UMaterialInterface*>& Materials, const FTransform& StaticMeshTransform, UGeometryCollection* GeometryCollectionObject, bool ReindexMaterials = true);

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
	*  Appends a GeometryCollection to a GeometryCollectionComponent.
	*  @param SourceGeometryCollection : Const GeometryCollection to read vertex/normals/index data from
	*  @param Materials : Materials fetched from the GeometryCollectionComponent used to configure this geometry
	*  @param GeometryCollectionTransform : GeometryCollection transform.
	*  @param TargetGeometryCollection  : Collection to append the GeometryCollection into.
	*/
	static void AppendGeometryCollection(const UGeometryCollection* SourceGeometryCollection, const TArray<UMaterialInterface*>& Materials, const FTransform& GeometryCollectionTransform, UGeometryCollection* TargetGeometryCollectionObject, bool ReindexMaterials = true);

	/**
	*  Appends a GeometryCollection to a GeometryCollectionComponent.
	*  @param GeometryCollectionComponent : Const GeometryCollection to read vertex/normals/index data from
	*  @param GeometryCollectionTransform : GeometryCollection transform.
	*  @param TargetGeometryCollection  : Collection to append the GeometryCollection into.
	*/
	static void AppendGeometryCollection(const UGeometryCollection* SourceGeometryCollection, const UGeometryCollectionComponent* GeometryCollectionComponent, const FTransform& GeometryCollectionTransform, UGeometryCollection* TargetGeometryCollectionObject, bool ReindexMaterials = true);

};