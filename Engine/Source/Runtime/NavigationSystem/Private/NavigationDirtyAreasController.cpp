// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavigationDirtyAreasController.h"
#include "NavigationData.h"

//----------------------------------------------------------------------//
// FNavigationDirtyAreasController
//----------------------------------------------------------------------//
FNavigationDirtyAreasController::FNavigationDirtyAreasController()
	: bCanAccumulateDirtyAreas(true)
#if !UE_BUILD_SHIPPING
	, bDirtyAreasReportedWhileAccumulationLocked(false)
#endif // !UE_BUILD_SHIPPING
{

}

void FNavigationDirtyAreasController::ForceRebuildOnNextTick()
{
	float MinTimeForUpdate = (DirtyAreasUpdateFreq != 0.f ? (1.0f / DirtyAreasUpdateFreq) : 0.f);
	DirtyAreasUpdateTime = FMath::Max(DirtyAreasUpdateTime, MinTimeForUpdate);
}

void FNavigationDirtyAreasController::Tick(const float DeltaSeconds, const TArray<ANavigationData*>& NavDataSet, bool bForceRebuilding)
{
	DirtyAreasUpdateTime += DeltaSeconds;
	const bool bCanRebuildNow = bForceRebuilding || (DirtyAreasUpdateFreq != 0.f && DirtyAreasUpdateTime >= (1.0f / DirtyAreasUpdateFreq));

	if (DirtyAreas.Num() > 0 && bCanRebuildNow)
	{
		for (ANavigationData* NavData : NavDataSet)
		{
			if (NavData)
			{
				NavData->RebuildDirtyAreas(DirtyAreas);
			}
		}

		DirtyAreasUpdateTime = 0.f;
		DirtyAreas.Reset();
	}
}

void FNavigationDirtyAreasController::AddArea(const FBox& NewArea, int32 Flags)
{
	if (Flags > 0 && bCanAccumulateDirtyAreas && NewArea.IsValid)
	{
		DirtyAreas.Add(FNavigationDirtyArea(NewArea, Flags));
	}
#if !UE_BUILD_SHIPPING
	bDirtyAreasReportedWhileAccumulationLocked = bDirtyAreasReportedWhileAccumulationLocked || (Flags > 0 && !bCanAccumulateDirtyAreas);
#endif // !UE_BUILD_SHIPPING
}

void FNavigationDirtyAreasController::Reset()
{
	// discard all pending dirty areas, we are going to rebuild navmesh anyway 
	DirtyAreas.Reset();
#if !UE_BUILD_SHIPPING
	bDirtyAreasReportedWhileAccumulationLocked = false;
#endif // !UE_BUILD_SHIPPING
}
