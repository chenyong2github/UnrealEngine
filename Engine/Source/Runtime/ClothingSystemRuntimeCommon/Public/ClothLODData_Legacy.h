// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothCollisionData.h"
#include "SkeletalMeshTypes.h"
#include "ClothVertBoneData.h"
#include "ClothPhysicalMeshData.h"

#include "ClothLODData_Legacy.generated.h"

class UClothLODDataCommon;
struct FPointWeightMap;

/**
 * Deprecated, legacy definition kept for backward compatibility only.
 * Use FPointWeightMap instead.
 * Redirected from the now defunct ClothingSystemRuntime module.
 */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMECOMMON_API FClothParameterMask_Legacy
{
	GENERATED_BODY();

	FClothParameterMask_Legacy();

	void MigrateTo(FPointWeightMap* Weights) const;

	/** Name of the mask, mainly for users to differentiate */
	UPROPERTY()
	FName MaskName;

	/** The currently targeted parameter for the mask */
	UPROPERTY()
	EWeightMapTargetCommon CurrentTarget;

	/** The maximum value currently in the mask value array */
	UPROPERTY()
	float MaxValue_DEPRECATED;

	/** The maximum value currently in the mask value array */
	UPROPERTY()
	float MinValue_DEPRECATED;

	/** The actual values stored in the mask */
	UPROPERTY()
	TArray<float> Values;

	/** Whether this mask is enabled and able to effect final mesh values */
	UPROPERTY()
	bool bEnabled;
};

/**
 * Deprecated, legacy definition kept for backward compatibility only.
 * Use UClothLODDataCommon instead.
 * Redirected from the now defunct ClothingSystemRuntime module.
 */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMECOMMON_API FClothLODData_Legacy
{
	GENERATED_BODY()

	// Raw phys mesh data
	UPROPERTY()
	FClothPhysicalMeshData PhysicalMeshData;

	// Collision primitive and covex data for clothing collisions
	UPROPERTY()
	FClothCollisionData CollisionData;

#if WITH_EDITORONLY_DATA
	// Parameter masks defining the physics mesh masked data
	UPROPERTY()
	TArray<FClothParameterMask_Legacy> ParameterMasks;
#endif // WITH_EDITORONLY_DATA

	// Skinning data for transitioning from a higher detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionUpSkinData;

	// Skinning data for transitioning from a lower detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionDownSkinData;

	bool Serialize(FArchive& Ar);

	// Migrate this legacy Nv struct to the new class format (called by UClothingAssetCommon::Postload())
	void MigrateTo(UClothLODDataCommon* LodData) const;
};

template<>
struct TStructOpsTypeTraits<FClothLODData_Legacy> : public TStructOpsTypeTraitsBase2<FClothLODData_Legacy>
{
	enum
	{
		WithSerializer = true,
	};
};
