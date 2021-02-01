// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothVertBoneData.h"
#include "PointWeightMap.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

#include "ClothPhysicalMeshData.generated.h"

class UClothPhysicalMeshDataBase_Legacy;
class UClothConfigBase;

/** Spatial simulation data for a mesh. */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMECOMMON_API FClothPhysicalMeshData
{
	GENERATED_BODY()

	/** Construct an empty cloth physical mesh with default common targets. */
	FClothPhysicalMeshData();

	/** Migrate from same, used to migrate LOD data from the UClothLODDataCommon_Legacy. */
	void MigrateFrom(FClothPhysicalMeshData& ClothPhysicalMeshData);

	/** Migrate from the legacy physical mesh data class, used to migrate LOD data from the UClothLODDataCommon_Legacy. */
	void MigrateFrom(UClothPhysicalMeshDataBase_Legacy* ClothPhysicalMeshDataBase);

	/** Reset the default common targets for this cloth physical mesh. */
	void Reset(const int32 InNumVerts, const int32 InNumIndices);

	/** Clear out any default weight maps and delete any other ones. */
	void ClearWeightMaps();

	/** Build the self collision indices for the relevant config. */
	void BuildSelfCollisionData(const TMap<FName, TObjectPtr<UClothConfigBase>>& ClothConfigs);

	/** Retrieve whether a vertex weight array has already been registered. */
	template<typename T>
	bool HasWeightMap(const T Target) const
	{ return WeightMaps.Contains((uint32)Target); }

	/** Retrieve a pointer to a registered vertex weight array by unique @param Id, or nullptr if none is found. */
	template<typename T>
	const FPointWeightMap* FindWeightMap(const T Target) const
	{ return WeightMaps.Find((uint32)Target); }

	/** Retrieve a pointer to a registered vertex weight array by unique @param Id, or nullptr if none is found. */
	template<typename T>
	FPointWeightMap* FindWeightMap(const T Target)
	{ return WeightMaps.Find((uint32)Target); }

	/** Retrieve a pointer to a registered vertex weight array by unique @param Id, or add one if it doesn't exist already. */
	template<typename T>
	FPointWeightMap& AddWeightMap(const T Target)
	{ return WeightMaps.Add((uint32)Target); }

	/** Retrieve a pointer to a registered vertex weight array by unique @param Id, or add one if it doesn't exist already. */
	template<typename T>
	FPointWeightMap& FindOrAddWeightMap(const T Target)
	{ return WeightMaps.FindOrAdd((uint32)Target); }

	/** Retrieve a registered vertex weight array by unique @param Id. The array must exists or this function will assert. */
	template<typename T>
	const FPointWeightMap& GetWeightMap(const T Target) const
	{ return WeightMaps[(uint32)Target]; }

	/** Retrieve a registered vertex weight array by unique @param Id. The array must exists or this function will assert. */
	template<typename T>
	FPointWeightMap& GetWeightMap(const T Target)
	{ return WeightMaps[(uint32)Target]; }

	// Positions of each simulation vertex
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<FVector> Vertices;

	// Normal at each vertex
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<FVector> Normals;

#if WITH_EDITORONLY_DATA
	// Color at each vertex
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<FColor> VertexColors;
#endif // WITH_EDITORONLY_DATA

	// Indices of the simulation mesh triangles
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<uint32> Indices;

	// The weight maps, or masks, used by this mesh, sorted by their target id
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TMap<uint32, FPointWeightMap> WeightMaps;

	// Inverse mass for each vertex in the physical mesh
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> InverseMasses;

	// Indices and weights for each vertex, used to skin the mesh to create the reference pose
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<FClothVertBoneData> BoneData;

	// Maximum number of bone weights of any vetex
	UPROPERTY(EditAnywhere, Category = SimMesh)
	int32 MaxBoneWeights;

	// Number of fixed verts in the simulation mesh (fixed verts are just skinned and do not simulate)
	UPROPERTY(EditAnywhere, Category = SimMesh)
	int32 NumFixedVerts;

	// Valid indices to use for self collisions (reduced set of Indices)
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<uint32> SelfCollisionIndices;

	// Deprecated. Use WeightMaps instead.
	UPROPERTY()
	TArray<float> MaxDistances_DEPRECATED;
	UPROPERTY()
	TArray<float> BackstopDistances_DEPRECATED;
	UPROPERTY()
	TArray<float> BackstopRadiuses_DEPRECATED;
	UPROPERTY()
	TArray<float> AnimDriveMultipliers_DEPRECATED;
};
