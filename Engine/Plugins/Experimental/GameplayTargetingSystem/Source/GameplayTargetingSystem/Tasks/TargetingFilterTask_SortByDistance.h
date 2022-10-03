// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameplayTargetingSystem/Types/TargetingSystemTypes.h"
#include "TargetingTask.h"
#include "UObject/Object.h"

#include "TargetingFilterTask_SortByDistance.generated.h"

class UTargetingSubsystem;
struct FTargetingDebugInfo;
struct FTargetingDefaultResultData;
struct FTargetingRequestHandle;


/**
*	@class UTargetingFilterTask_SortByDistance
*
*	Simple sorting filter based on the distance to the source actor.
*	Note: This filter will use the FTargetingDefaultResultsSet and 
*	use the Score factor defined for each target to store the distance
*	and sort by that value.
*/
UCLASS(Blueprintable)
class UTargetingFilterTask_SortByDistance : public UTargetingTask
{
	GENERATED_BODY()

public:
	UTargetingFilterTask_SortByDistance(const FObjectInitializer& ObjectInitializer);

	/** Evaluation function called by derived classes to process the targeting request */
	virtual void Execute(const FTargetingRequestHandle& TargetingHandle) const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Target Sorting | Data")
	uint8 bAscending : 1;

	/** Debug Helper Methods */
#if ENABLE_DRAW_DEBUG
private:
	virtual void DrawDebug(UTargetingSubsystem* TargetingSubsystem, FTargetingDebugInfo& Info, const FTargetingRequestHandle& TargetingHandle, float XOffset, float YOffset, int32 MinTextRowsToAdvance) const override;
	void BuildPreSortDebugString(const FTargetingRequestHandle& TargetingHandle, const TArray<FTargetingDefaultResultData>& TargetResults) const;
	void BuildPostSortDebugString(const FTargetingRequestHandle& TargetingHandle, const TArray<FTargetingDefaultResultData>& TargetResults) const;
	void ResetSortDebugStrings(const FTargetingRequestHandle& TargetingHandle) const;
#endif // ENABLE_DRAW_DEBUG
	/** ~Debug Helper Methods */
};

