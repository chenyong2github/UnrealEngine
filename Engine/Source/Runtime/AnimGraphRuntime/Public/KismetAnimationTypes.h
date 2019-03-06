// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetAnimationTypes.generated.h"

/**
 *	An easing type defining how to ease float values.
 */
UENUM(meta = (DocumentationPolicy = "Strict"))
enum class EEasingFuncType : uint8
{
	// Linear easing (no change to the value)
	Linear,
	// Easing using a sinus function
	Sinusoidal,
	// Cubic version of the value (only in)
	Cubic,
	// Quadratic version of the value (in and out)
	QuadraticInOut,
	// Cubic version of the value (in and out)
	CubicInOut,
	// Easing using a cubic hermite function
	HermiteCubic,
	// Quartic version of the value (in and out)
	QuarticInOut,
	// Quintic version of the value (in and out)
	QuinticInOut,
	// Circular easing (only in)
	CircularIn,
	// Circular easing (only out)
	CircularOut,
	// Circular easing (in and out)
	CircularInOut,
	// Exponential easing (only in)
	ExpIn,
	// Exponential easing (only out)
	ExpOut,
	// Exponential easing (in and out)
	ExpInOut
};

/**
 *	The FPositionHistory is a container to record position changes
 *	over time. This is used to calculate velocity of a bone, for example.
 *	The FPositionArray also tracks the last index used to allow for
 *	reuse of entries (instead of appending to the array all the time).
 */
USTRUCT(BlueprintType, meta = (DocumentationPolicy = "Strict"))
struct FPositionHistory
{
	GENERATED_BODY()

public:

	/** Default constructor */
	FPositionHistory()
		: Positions(TArray<FVector>())
		, Velocities(TArray<float>())
		, LastIndex(0)
	{}

	/** The recorded positions */
	UPROPERTY(EditAnywhere, Category = "FPositionHistory", meta = (MetaClass = "AdvancedCopyCustomization"))
	TArray<FVector> Positions;

	/** The range for this particular history */
	UPROPERTY(EditAnywhere, Category = "FPositionHistory", meta = (MetaClass = "AdvancedCopyCustomization", UIMin = "0.0", UIMax = "1.0"))
	float Range;

	TArray<float> Velocities;
	uint32 LastIndex; /// The last index used to store a position
};

