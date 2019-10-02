// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothPhysicalMeshData.h"

#include "ClothCollisionData.h"
#include "PointWeightMap.h"

#include "SkeletalMeshTypes.h"

#include "ClothLODData.generated.h"

/** */
UCLASS()
class CLOTHINGSYSTEMRUNTIMECOMMON_API UClothLODDataBase : public UObject
{
	GENERATED_BODY()
public:
	UClothLODDataBase(const FObjectInitializer& Init);
	virtual ~UClothLODDataBase();

	// Raw phys mesh data
	UPROPERTY(EditAnywhere, Category = SimMesh)
	UClothPhysicalMeshDataBase* PhysicalMeshData;

	// Collision primitive and covex data for clothing collisions
	UPROPERTY(EditAnywhere, Category = Collision)
	FClothCollisionData CollisionData;

#if WITH_EDITORONLY_DATA
	// Parameter masks defining the physics mesh masked data
	UPROPERTY(EditAnywhere, Category = Masks)
	TArray<FPointWeightMap> ParameterMasks;

	// Get all available parameter masks for the specified target
	//void GetParameterMasksForTarget(const MaskTarget_PhysMesh& InTarget, TArray<FPointWeightMap*>& OutMasks);
	void GetParameterMasksForTarget(const uint8 InTarget, TArray<FPointWeightMap*>& OutMasks);
#endif // WITH_EDITORONLY_DATA
#if WITH_EDITOR
	/** Copy \c ParameterMasks to corresponding targets in \c PhysicalMeshData. */
	void PushWeightsToMesh();
#endif
	// Skinning data for transitioning from a higher detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionUpSkinData;

	// Skinning data for transitioning from a lower detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionDownSkinData;

	virtual void Serialize(FArchive& Ar) override;
};
