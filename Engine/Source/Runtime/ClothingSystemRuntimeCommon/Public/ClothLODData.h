// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothPhysicalMeshData.h"
#include "ClothPhysicalMeshDataBase_Legacy.h"
#include "ClothCollisionData.h"
#include "PointWeightMap.h"
#include "SkeletalMeshTypes.h"

#include "ClothLODData.generated.h"

/** Common Cloth LOD representation for all clothing assets. */
UCLASS()
class CLOTHINGSYSTEMRUNTIMECOMMON_API UClothLODDataCommon : public UObject
{
	GENERATED_BODY()
public:
	UClothLODDataCommon(const FObjectInitializer& Init);
	virtual ~UClothLODDataCommon();

	// Deprecated, use ClothPhysicalMeshData instead
	UPROPERTY()
	UClothPhysicalMeshDataBase_Legacy* PhysicalMeshData_DEPRECATED;

	// Raw phys mesh data
	UPROPERTY(EditAnywhere, Category = SimMesh)
	FClothPhysicalMeshData ClothPhysicalMeshData;

	// Collision primitive and covex data for clothing collisions
	UPROPERTY(EditAnywhere, Category = Collision)
	FClothCollisionData CollisionData;

#if WITH_EDITORONLY_DATA
	// Parameter masks defining the physics mesh masked data
	UPROPERTY(EditAnywhere, Category = Masks)
	TArray<FPointWeightMap> ParameterMasks;

	// Get all available parameter masks for the specified target
	void GetParameterMasksForTarget(const uint8 InTarget, TArray<FPointWeightMap*>& OutMasks);
#endif // WITH_EDITORONLY_DATA
#if WITH_EDITOR
	/** Copy \c ParameterMasks to corresponding targets in \c ClothPhysicalMeshData. */
	void PushWeightsToMesh();
#endif
	// Skinning data for transitioning from a higher detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionUpSkinData;

	// Skinning data for transitioning from a lower detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionDownSkinData;

	// Custom serialize for transition
	virtual void Serialize(FArchive& Ar) override;

	// Migrate deprecated properties
	virtual void PostLoad() override;
};
