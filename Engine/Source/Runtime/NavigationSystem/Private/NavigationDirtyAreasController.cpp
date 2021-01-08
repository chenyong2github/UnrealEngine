// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavigationDirtyAreasController.h"
#include "NavigationData.h"

DEFINE_LOG_CATEGORY_STATIC(LogNavigationDirtyArea, Warning, All);

//----------------------------------------------------------------------//
// FNavigationDirtyAreasController
//----------------------------------------------------------------------//
FNavigationDirtyAreasController::FNavigationDirtyAreasController()
	: bCanAccumulateDirtyAreas(true)
#if !UE_BUILD_SHIPPING
	, bDirtyAreasReportedWhileAccumulationLocked(false)
	, bCanReportOversizedDirtyArea(false)
	, bNavigationBuildLocked(false)
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

void FNavigationDirtyAreasController::AddArea(const FBox& NewArea, const int32 Flags, const TFunction<UObject*()>& ObjectProviderFunc /*= nullptr*/)
{
#if !UE_BUILD_SHIPPING
	// always keep track of reported areas even when filtered out by invalid area as long as flags are valid
	bDirtyAreasReportedWhileAccumulationLocked = bDirtyAreasReportedWhileAccumulationLocked || (Flags > 0 && !bCanAccumulateDirtyAreas);
#endif // !UE_BUILD_SHIPPING

	if (!NewArea.IsValid)
	{
		UE_LOG(LogNavigationDirtyArea, Warning, TEXT("Skipping dirty area creation because of invalid bounds (object: %s)"), *GetFullNameSafe(ObjectProviderFunc ? ObjectProviderFunc() : nullptr));
		return;
	}

	const FVector2D BoundsSize(NewArea.GetSize());
	if (BoundsSize.IsNearlyZero())
	{
		UE_LOG(LogNavigationDirtyArea, Warning, TEXT("Skipping dirty area creation because of empty bounds (object: %s)"), *GetFullNameSafe(ObjectProviderFunc ? ObjectProviderFunc() : nullptr));
		return;
	}

#if !UE_BUILD_SHIPPING
	auto DumpExtraInfo = [ObjectProviderFunc, BoundsSize]() {
		const UObject* ObjectProvider = nullptr;
		if (ObjectProviderFunc)
		{
			ObjectProvider = ObjectProviderFunc();
		}

		const UActorComponent* ObjectAsComponent = Cast<UActorComponent>(ObjectProvider);
		const AActor* ComponentOwner = ObjectAsComponent ? ObjectAsComponent->GetOwner() : nullptr;
		return FString::Printf(TEXT("Adding dirty area object = % | Potential component's owner = %s | Bounds size = %s)"), *GetFullNameSafe(ObjectProvider), *GetFullNameSafe(ComponentOwner), *BoundsSize.ToString());
	};
	UE_LOG(LogNavigationDirtyArea, VeryVerbose, TEXT("%s"), *DumpExtraInfo());

	if (ShouldReportOversizedDirtyArea() && BoundsSize.GetMax() > DirtyAreaWarningSizeThreshold)
	{
		UE_LOG(LogNavigationDirtyArea, Warning, TEXT("Adding an oversized dirty area (object:%s size:%s threshold:%.2f)"),
			*GetFullNameSafe(ObjectProviderFunc ? ObjectProviderFunc() : nullptr),
			*BoundsSize.ToString(),
			DirtyAreaWarningSizeThreshold);
	}
#endif // !UE_BUILD_SHIPPING

	if (Flags > 0 && bCanAccumulateDirtyAreas)
	{
		DirtyAreas.Add(FNavigationDirtyArea(NewArea, Flags));
	}
}

void FNavigationDirtyAreasController::OnNavigationBuildLocked()
{
#if !UE_BUILD_SHIPPING
	bNavigationBuildLocked = true;
#endif // !UE_BUILD_SHIPPING
}

void FNavigationDirtyAreasController::OnNavigationBuildUnlocked()
{
#if !UE_BUILD_SHIPPING
	bNavigationBuildLocked = false;
#endif // !UE_BUILD_SHIPPING
}

void FNavigationDirtyAreasController::SetDirtyAreaWarningSizeThreshold(const float Threshold)
{
#if !UE_BUILD_SHIPPING
	DirtyAreaWarningSizeThreshold = Threshold;
#endif // !UE_BUILD_SHIPPING
}

void FNavigationDirtyAreasController::SetCanReportOversizedDirtyArea(bool bCanReport)
{
#if !UE_BUILD_SHIPPING
	bCanReportOversizedDirtyArea = bCanReport;
#endif // !UE_BUILD_SHIPPING
}

#if !UE_BUILD_SHIPPING
bool FNavigationDirtyAreasController::ShouldReportOversizedDirtyArea() const
{ 
	return bNavigationBuildLocked == false && bCanReportOversizedDirtyArea && DirtyAreaWarningSizeThreshold >= 0.0f;
}
#endif // !UE_BUILD_SHIPPING


void FNavigationDirtyAreasController::Reset()
{
	// discard all pending dirty areas, we are going to rebuild navmesh anyway 
	DirtyAreas.Reset();
#if !UE_BUILD_SHIPPING
	bDirtyAreasReportedWhileAccumulationLocked = false;
#endif // !UE_BUILD_SHIPPING
}
