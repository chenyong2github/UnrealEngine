// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimationModifier.h"
#include "DistanceCurveModifier.generated.h"

/** Extracts traveling distance information from root motion and bakes it to a curve.
 * A negative value indicates distance remaining to a stop or pivot point.
 * A positive value indicates distance traveled from a start point or from the beginning of the clip.
 */
UCLASS()
class UDistanceCurveModifier : public UAnimationModifier
{
	GENERATED_BODY()

public:

	/** Rate used to sample the animation. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin = "1"))
	int32 SampleRate = 30;

	/** Name for the generated curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName CurveName = "Distance";

	/** Root motion speed must be below this threshold to be considered stopped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	float StopSpeedThreshold = 5.0f;

	virtual void OnApply_Implementation(UAnimSequence* Animation) override;
	virtual void OnRevert_Implementation(UAnimSequence* Animation) override;
};