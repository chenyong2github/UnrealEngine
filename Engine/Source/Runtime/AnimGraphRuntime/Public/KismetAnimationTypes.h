// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetAnimationTypes.generated.h"

// The kind of easing function to use
UENUM()
enum class EEasingFuncType : uint8
{
	Linear,
	Sinusoidal,
	Cubic,
	QuadraticInOut,
	CubicInOut,
	HermiteCubic,
	QuarticInOut,
	QuinticInOut,
	CircularIn,
	CircularOut,
	CircularInOut,
	ExpIn,
	ExpOut,
	ExpInOut
};

USTRUCT(BlueprintType)
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

	TArray<float> Velocities;
	uint32 LastIndex; /// The last index used to store a position
};

