// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PointWeightMap.h"
#include "ClothPhysicalMeshDataNv.h"
#include "ClothParameterMask_PhysMesh.generated.h"

/** Deprecated.  Use FPointWeightMap instead. */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMENV_API FClothParameterMask_PhysMesh
{
	GENERATED_BODY();

	FClothParameterMask_PhysMesh()
		: MaskName(NAME_None)
		, CurrentTarget(MaskTarget_PhysMesh::None)
		, MaxValue_DEPRECATED(0.0)
		, MinValue_DEPRECATED(100.0)
		, bEnabled(false)
	{}

	void MigrateTo(FPointWeightMap* Weights) const
	{
		Weights->Name = MaskName;
		Weights->CurrentTarget = static_cast<uint8>(CurrentTarget);
		Weights->Values = Values;
		Weights->bEnabled = bEnabled;
	}

	/** Name of the mask, mainly for users to differentiate */
	UPROPERTY()
	FName MaskName;

	/** The currently targeted parameter for the mask */
	UPROPERTY()
	MaskTarget_PhysMesh CurrentTarget;

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
