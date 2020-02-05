// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraGPUSortInfo.h: GPU particle sorting helper
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "GPUSortManager.h"
#include "NiagaraGPUSortInfo.generated.h"

UENUM()
enum class ENiagaraSortMode : uint8
{
	/** Perform no additional sorting prior to rendering.*/
	None,
	/** Sort by depth to the camera's near plane.*/
	ViewDepth,
	/** Sort by distance to the camera's origin.*/
	ViewDistance,
	/** Custom sorting according to a per particle attribute. Which attribute is defined by the renderer's CustomSortingBinding which defaults to Particles.NormalizedAge. Lower values are rendered before higher values. */
	CustomAscending,
	/** Custom sorting according to a per particle attribute. Which attribute is defined by the renderer's CustomSortingBinding which defaults to Particles.NormalizedAge. Higher values are rendered before lower values. */
	CustomDecending,
};

struct FNiagaraGPUSortInfo
{
	// The number of particles in the system.
	int32 ParticleCount = 0;
	// How the particles should be sorted.
	ENiagaraSortMode SortMode = ENiagaraSortMode::None;
	// On which attribute to base the sorting
	int32 SortAttributeOffset = INDEX_NONE;
	// The data buffer that holds the particle attributes.
	FShaderResourceViewRHIRef ParticleDataFloatSRV;
	uint32 FloatDataStride = 0;
	// The actual GPU sim particle count. Needed to get an exact match on the index list.
	FShaderResourceViewRHIRef GPUParticleCountSRV;
	uint32 GPUParticleCountOffset = INDEX_NONE;
	// View data.
	FVector ViewOrigin;
	FVector ViewDirection;

	// The GPUSortManager bindings for this sort task.
	FGPUSortManager::FAllocationInfo AllocationInfo;
	// The sort constraints for the task in GPUSortManager.
	EGPUSortFlags SortFlags = EGPUSortFlags::None;

	// Set the SortFlags based on the emitter and material constraints.
	FORCEINLINE void SetSortFlags(bool bHighPrecisionKeys, bool bTranslucentMaterial)
	{
		SortFlags = EGPUSortFlags::KeyGenAfterPreRender | EGPUSortFlags::ValuesAsInt32 | 
			(bHighPrecisionKeys ? EGPUSortFlags::HighPrecisionKeys : EGPUSortFlags::LowPrecisionKeys) |
			(bTranslucentMaterial ? EGPUSortFlags::AnySortLocation : EGPUSortFlags::SortAfterPreRender);
	}
};