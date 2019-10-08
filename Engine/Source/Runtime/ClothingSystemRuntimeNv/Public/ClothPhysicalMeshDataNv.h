// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothPhysicalMeshData.h"
#include "ClothPhysicalMeshDataNv.generated.h"

/** The possible targets for a physical mesh point weight map. */
UENUM()
enum class MaskTarget_PhysMesh : uint8
{
	None = 0,			// None should always be zero
	MaxDistance,
	BackstopDistance,
	BackstopRadius,
	AnimDriveMultiplier
};

/** NV specific spatial simulation data for a mesh. */
UCLASS()
class CLOTHINGSYSTEMRUNTIMENV_API UClothPhysicalMeshDataNv : public UClothPhysicalMeshDataBase
{
	GENERATED_BODY()
public:
	UClothPhysicalMeshDataNv();
	virtual ~UClothPhysicalMeshDataNv();

	void
	Reset(const int32 NumPoints) override;

	void
	ClearParticleParameters() override;

	virtual void
	BuildSelfCollisionData(const UClothConfigBase* ClothConfig) override;

	virtual UEnum* GetFloatArrayTargets() const override
	{ return StaticEnum<MaskTarget_PhysMesh>(); }

	virtual bool IsFullyKinematic() const override
	{ return MaxDistances.Num() == 0; }
	
	virtual bool IsFixed(const uint16 X, const float Threshold=0.1f) const override
	{ return MaxDistances.IsValidIndex(X) && MaxDistances[X] <= Threshold; }

	virtual bool IsFixed(const uint16 X, const uint16 Y, const uint16 Z, const float Threshold=0.1f) const override
	{ return IsFixed(X, Threshold) || IsFixed(Y, Threshold) || IsFixed(Z, Threshold); }

	// The distance that each vertex can move away from its reference (skinned) position
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> MaxDistances;

	// Distance along the plane of the surface that the particles can travel (separation constraint)
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> BackstopDistances;

	// Radius of movement to allow for backstop movement
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> BackstopRadiuses;

	// Strength of anim drive per-particle (spring driving particle back to skinned location
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> AnimDriveMultipliers;
};


/** Deprecated.  Use UClothPhysicalMeshDataNv instead. */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMENV_API FClothPhysicalMeshData
{
	GENERATED_BODY()

	void Reset(const int32 InNumVerts);

	// Clear out any target properties in this physical mesh
	void ClearParticleParameters();

	// Whether the mesh uses backstops
	bool HasBackStops() const;

	// Whether the mesh uses anim drives
	bool HasAnimDrive() const;

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

	// The distance that each vertex can move away from its reference (skinned) position
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> MaxDistances;

	// Distance along the plane of the surface that the particles can travel (separation constraint)
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> BackstopDistances;

	// Radius of movement to allow for backstop movement
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> BackstopRadiuses;

	// Strength of anim drive per-particle (spring driving particle back to skinned location
	UPROPERTY(EditAnywhere, Category = SimMesh)
	TArray<float> AnimDriveMultipliers;

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

	void MigrateTo(UClothPhysicalMeshDataBase* MeshData) const
	{
		MeshData->Vertices = Vertices;
		MeshData->Normals = Normals;
#if WITH_EDITORONLY_DATA
		MeshData->VertexColors = VertexColors;
#endif
		MeshData->Indices = Indices;
		MeshData->InverseMasses = InverseMasses;
		MeshData->BoneData = BoneData;
		MeshData->NumFixedVerts = NumFixedVerts;
		MeshData->MaxBoneWeights = MaxBoneWeights;
		MeshData->SelfCollisionIndices = SelfCollisionIndices;
		if (UClothPhysicalMeshDataNv* NvMeshData = Cast<UClothPhysicalMeshDataNv>(MeshData))
		{
			NvMeshData->MaxDistances = MaxDistances;
			NvMeshData->BackstopDistances = BackstopDistances;
			NvMeshData->BackstopRadiuses = BackstopRadiuses;
			NvMeshData->AnimDriveMultipliers = AnimDriveMultipliers;
		}
		else
		{
			unimplemented();
		}
	}
};
