// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "AI/Navigation/NavigationTypes.h"


class ANavigationData;

struct NAVIGATIONSYSTEM_API FNavigationDirtyAreasController
{
	/** update frequency for dirty areas on navmesh */
	float DirtyAreasUpdateFreq = 60.f;

	/** temporary cumulative time to calculate when we need to update dirty areas */
	float DirtyAreasUpdateTime = 0.f;

	/** stores areas marked as dirty throughout the frame, processes them
 *	once a frame in Tick function */
	TArray<FNavigationDirtyArea> DirtyAreas;

	uint8 bCanAccumulateDirtyAreas : 1;
#if !UE_BUILD_SHIPPING
	uint8 bDirtyAreasReportedWhileAccumulationLocked : 1;
#endif // !UE_BUILD_SHIPPING

	FNavigationDirtyAreasController();

	void Reset();
	
	/** sets cumulative time to at least one cycle so next tick will rebuild dirty areas */
	void ForceRebuildOnNextTick();

	void Tick(float DeltaSeconds, const TArray<ANavigationData*>& NavDataSet, bool bForceRebuilding = false);
	void AddArea(const FBox& NewArea, int32 Flags);
	
	bool IsDirty() const { return GetNumDirtyAreas() > 0; }
	int32 GetNumDirtyAreas() const { return DirtyAreas.Num(); }

#if !UE_BUILD_SHIPPING
	bool HadDirtyAreasReportedWhileAccumulationLocked() const { return bCanAccumulateDirtyAreas == false && bDirtyAreasReportedWhileAccumulationLocked; }
#endif // UE_BUILD_SHIPPING
};