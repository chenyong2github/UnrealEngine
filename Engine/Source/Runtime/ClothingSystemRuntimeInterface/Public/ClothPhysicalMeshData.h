// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"

#include "ClothVertBoneData.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

#include "ClothPhysicalMeshData.generated.h"

class UClothConfigBase;

/**
 * Simulation mesh points, topology, and spatial parameters defined on that 
 * topology.
 *
 * Created curing asset import or created from a skeletal mesh.
 */
UCLASS()
class CLOTHINGSYSTEMRUNTIMEINTERFACE_API UClothPhysicalMeshDataBase : public UObject
{
	GENERATED_BODY()
public:
	UClothPhysicalMeshDataBase();
	virtual ~UClothPhysicalMeshDataBase();

	virtual void 
	Reset(const int32 InNumVerts);

	// TODO: Ryan - Rename to ClearWeightedParameters()?
	/** Clear out any target properties in this physical mesh. */
	virtual void 
	ClearParticleParameters()
	{}

	/** Callback invoked from \c UClothingAssetBase::BuildSelfCollisionData(), 
	 * implement in derived classes. 
	 */
	virtual void 
	BuildSelfCollisionData(const UClothConfigBase* ClothConfig)
	{ unimplemented(); }

	/** Retrieve a registered vertex weight array by unique @param Id. */
	TArray<float>* 
	GetFloatArray(const uint32 Id) const;

	/** Get ids for all registered weight arrays. */
	TArray<uint32>
	GetFloatArrayIds() const;

	/** Get all registered weight arrays. */
	TArray<TArray<float>*>
	GetFloatArrays() const;

	/** Returns an \c Enum mapping float array Id's to names used in the UI. */
	virtual UEnum* 
	GetFloatArrayTargets() const
	{ unimplemented(); return nullptr; }


	virtual bool IsFullyKinematic() const
	{ unimplemented(); return true; }
	virtual bool IsFixed(const uint16 X, const float Threshold=0.1f) const
	{ unimplemented(); return false; }
	virtual bool IsFixed(const uint16 X, const uint16 Y, const uint16 Z, const float Threshold=0.1f) const
	{ unimplemented(); return false; }

protected:
	/** Register an @param Array keyed by a unique @param Id. */
	void RegisterFloatArray(const uint32 Id, TArray<float> *Array);

public:
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

	// Inverse mass for each vertex in the physical mesh
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> InverseMasses;

	// Indices and weights for each vertex, used to skin the mesh to create the reference pose
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<FClothVertBoneData> BoneData;

	// Number of fixed verts in the simulation mesh (fixed verts are just skinned and do not simulate)
	UPROPERTY(EditAnywhere, Category = SimMesh)
	int32 NumFixedVerts;

	// Maximum number of bone weights of any vetex
	UPROPERTY(EditAnywhere, Category = SimMesh)
	int32 MaxBoneWeights;

	// Valid indices to use for self collisions (reduced set of Indices)
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<uint32> SelfCollisionIndices;

private:
	TMap<uint32, TArray<float>*> IdToArray;
};
