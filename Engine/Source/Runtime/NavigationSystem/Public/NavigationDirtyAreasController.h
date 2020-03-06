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
private:
	uint8 bCanReportOversizedDirtyArea : 1;
	uint8 bNavigationBuildLocked : 1;

	/** -1 by default, if set to a positive value dirty area with bounds size over that threshold will be logged */
	float DirtyAreaWarningSizeThreshold = -1.f;

	bool ShouldReportOversizedDirtyArea() const;
#endif // !UE_BUILD_SHIPPING

public:
	FNavigationDirtyAreasController();

	void Reset();
	
	/** sets cumulative time to at least one cycle so next tick will rebuild dirty areas */
	void ForceRebuildOnNextTick();

	void Tick(float DeltaSeconds, const TArray<ANavigationData*>& NavDataSet, bool bForceRebuilding = false);

	/** Add a dirty area to the queue based on the provided bounds and flags.
	 * Bounds must be valid and non empty otherwise the request will be ignored and a warning reported.
	 * Accumulation must be allowed and flags valid otherwise the add is ignored.
	 *	@param NewArea Bounding box of the affected area
	 *	@param Flags Indicates the type of modification applied to the area
	 *	@param ObjectProviderFunc Optional function to retrieve source object that can be use for error reporting
	 */
	void AddArea(const FBox& NewArea, const int32 Flags, const TFunction<UObject*()>& ObjectProviderFunc = nullptr);
	
	bool IsDirty() const { return GetNumDirtyAreas() > 0; }
	int32 GetNumDirtyAreas() const { return DirtyAreas.Num(); }

	void OnNavigationBuildLocked();
	void OnNavigationBuildUnlocked();

	void SetCanReportOversizedDirtyArea(const bool bCanReport);
	void SetDirtyAreaWarningSizeThreshold(const float Threshold);

#if !UE_BUILD_SHIPPING
	bool HadDirtyAreasReportedWhileAccumulationLocked() const { return bCanAccumulateDirtyAreas == false && bDirtyAreasReportedWhileAccumulationLocked; }
#endif // UE_BUILD_SHIPPING
};