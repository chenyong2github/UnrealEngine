// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/InputScaleBias.h"
#include "PoseSearchPredictionTypes.generated.h"

USTRUCT(BlueprintType, Category = "Animation|Prediction")
struct POSESEARCH_API FPredictionPlayRateAdjustment
{
	GENERATED_BODY()

	// Optional scale, bias, and clamp play rate value adjustment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinShownByDefault))
	FInputScaleBiasClamp ScaleBiasClamp;

	// Optional play rate value remapping curve
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	TObjectPtr<const UCurveFloat> RemappingCurve = nullptr;

	// Computes a new play rate by first applying the remapping curve and then applying the scale/bias/clamp adjustment
	float ComputePlayRate(float PlayRate, float DeltaTime) const;
};

USTRUCT(BlueprintType, Category = "Animation|Prediction")
struct POSESEARCH_API FPredictionTrajectoryState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = Prediction)
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = Prediction)
	FVector LocalLinearVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = Prediction)
	FVector LocalLinearAcceleration = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = Prediction)
	float AccumulatedDistance = 0.0;

	bool IsZeroSample() const;

	static FPredictionTrajectoryState Lerp(const FPredictionTrajectoryState& A, const FPredictionTrajectoryState& B, float Alpha);
};

USTRUCT(BlueprintType, Category = "Animation|Prediction")
struct POSESEARCH_API FPredictionTrajectoryRange
{
	GENERATED_BODY()

	// Contains the per-frame range of predicted trajectory states
	UPROPERTY(BlueprintReadOnly, Category = Prediction)
	TArray<FPredictionTrajectoryState> Samples;

	bool HasSamples() const;

	// Useful in detecting idle motion states
	bool HasOnlyZeroSamples() const;
};

USTRUCT(BlueprintType, Category = "Animation|Prediction")
struct POSESEARCH_API FPredictionTrajectorySettings
{
	GENERATED_BODY()
	
	// Optional play rate scaling adjustment applied as a post process
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinShownByDefault))
	FPredictionPlayRateAdjustment PlayRateAdjustment;

	// Root motion time step used for analyzing and determining future velocity minima
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault, ClampMin = "15", ClampMax = "240"))
	float RootMotionSampleStepPerSecond = 120.f;

	// Root motion angle threshold for determining significant changes in direction (pivots)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault, ClampMin = "0", ClampMax = "180"))
	float ZeroRootMotionAngleThreshold = 90.f;

	// Root motion displacement error tolerance for identifying pivot false positives
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	float ZeroRootMotionDisplacementError = 0.0004f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Debug)
	bool bDebugDraw = false;
#endif
};

USTRUCT(BlueprintInternalUseOnly, Category = "Animation|Prediction")
struct POSESEARCH_API FPredictionSequenceState
{
	GENERATED_BODY()

	// Current evaluating sequence (currently for use in Motion Matching)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = State, meta = (PinShownByDefault))
	TObjectPtr<const UAnimSequenceBase> SequenceBase = nullptr;

	// Internal sequence accumulated time
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = State, meta = (PinShownByDefault))
	float AccumulatedTime = 0.f;

	// Internal sequence play rate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = State, meta = (PinShownByDefault))
	float PlayRate = 1.f;


	// Looping or non-looping sequence
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = State, meta = (PinShownByDefault))
	bool bLooping = false;

	bool HasSequence() const;
};