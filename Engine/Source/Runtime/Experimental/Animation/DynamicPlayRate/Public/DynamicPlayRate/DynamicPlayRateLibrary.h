// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/InputScaleBias.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "Curves/CurveFloat.h"
#include "UObject/ObjectMacros.h"

#include "DynamicPlayRateLibrary.generated.h"

USTRUCT(BlueprintType, Category="Dynamic Play Rate")
struct DYNAMICPLAYRATE_API FDynamicPlayRateSettings
{
	GENERATED_BODY()

	// Enable/Disable dynamic play rate adjusment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PinShownByDefault))
	bool bEnabled = false;

	// Optional scaling, biasing, and clamping controls for play rate adjustment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	FInputScaleBiasClamp ScaleBiasClamp;

	// Optional play rate remapping curve (X axis: Source play rate, Y axis: Target play rate)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	TObjectPtr<const UCurveFloat> RemappingCurve = nullptr;

	// Root motion time step (per second) used for analyzing and determining future zero velocities
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(ClampMin="15.0", ClampMax="120.0"))
	float RootMotionSampleRate = 60.f;

	// Root motion angle threshold for determining significant changes in direction (ex: Identifying pivots)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(ClampMin="0.0", ClampMax="180.0"))
	float ZeroRootMotionAngleThreshold = 90.f;

	// Root motion displacement error tolerance, for identifying false positive associated with ZeroRootMotionAngleThreshold
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	float ZeroRootMotionDisplacementError = 0.0004f;

	// Computes a new play rate by first attempting the remapping curve and then defaulting to the scale/bias/clamp control adjustment
	float ComputePlayRate(float PlayRate, float DeltaTime) const;

#if WITH_EDITORONLY_DATA
	// Display in-game world markers visualizing the most significant motion values in consideration for play rate adjustment
	UPROPERTY(EditAnywhere, Category=Debug)
	bool bDebugDraw = false;
#endif
};

/**
* Computes a dynamically adjusted play rate value for a given playing sequence asset dependent on:
*	1. Analysis of the root motion vs prediction motion contribution ratio
*	2. Determination of significant events such as stopping, pivoting, and motion discontinuity
*
* @param UpdateContext	Input animation update context providing access to the proxy and delta time
* @param Trajectory		Input trajectory samples used for predictive motion analysis against the chosen sequence root motion track
* @param Settings		Input dynamic play rate adjustment settings for specifying error tolerances, sample rate, and additional play rate behavior
* @param Sequence		Input playing sequence asset
* @param PlayRate		Input sequence play rate, prior to adjustment
* @param bLooping		Input sequence looping behavior
*
* @return				Adjusted play rate value
*/
DYNAMICPLAYRATE_API float DynamicPlayRateAdjustment(const FAnimationUpdateContext& Context
	, FTrajectorySampleRange Trajectory
	, const FDynamicPlayRateSettings& Settings
	, const UAnimSequenceBase* Sequence
	, float AccumulatedTime
	, float PlayRate
	, bool bLooping
);

UCLASS(Category="Dynamic Play Rate")
class DYNAMICPLAYRATE_API UDynamicPlayRateLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Computes a dynamically adjusted play rate value for a given playing sequence asset
	UFUNCTION(BlueprintPure, Category="Motion Trajectory", meta=(BlueprintThreadSafe))
	static float DynamicPlayRateAdjustment(const FAnimUpdateContext& Context
	    , FTrajectorySampleRange Trajectory
		, const FDynamicPlayRateSettings& Settings
		, const UAnimSequenceBase* Sequence
		, float AccumulatedTime
		, float PlayRate
		, bool bLooping
	);
};

