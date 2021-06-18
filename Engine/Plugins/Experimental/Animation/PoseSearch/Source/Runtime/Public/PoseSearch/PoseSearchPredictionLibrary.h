// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Note: Various includes, class and function annotations have been commented out as reference
// for using this functionality in the context of the run-time "Encapsulation" initiative

#include "CoreMinimal.h"
#include "PoseSearchPredictionTypes.h"
// #include "UObject/ObjectMacros.h"
// #include "PoseSearchPredictionLibrary.generated.h"

// UCLASS()
class UPoseSearchPredictionDistanceMatching // : public UBlueprintFunctionLibrary
{
	// GENERATED_BODY()

public:
	// Computes the effective play rate of a sequence by modeling and analyzing the ratio of the capsule trajectory prediction 
	// vs sequence root motion, or vice versa.
	//
	// Currently this has been tested against a variety of locomotion features such as:
	// 1) Starts, Stops, Pivots, and Cycles
	// 2) Differing, similar, or divergent prediction and root motion velocity/acceleration models

	// UFUNCTION(BlueprintCallable, Category = "Distance Matching")
	static float ComputePlayRate(const FAnimationUpdateContext& Context
		, const FPredictionTrajectoryRange& TrajectoryRange
		, const FPredictionTrajectorySettings& Settings
		, const FPredictionSequenceState& SequenceState);
};
