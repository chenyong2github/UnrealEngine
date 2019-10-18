// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapLightEstimationTypes.generated.h"

/** Which camera the information is related to. */
UENUM(BlueprintType)
enum class EMagicLeapLightEstimationCamera : uint8
{
	/** Left. */
	Left,
	/** Right. */
	Right,
	/** Far left. */
	FarLeft,
	/** Far right. */
	FarRight
};

/**
	Information about the ambient light sensor global state.
	Capturing images or video will stop the lighting information update,
	causing the retrieved data to be stale (old timestamps).
*/
USTRUCT(BlueprintType)
struct FMagicLeapLightEstimationAmbientGlobalState
{
	GENERATED_BODY()

public:
	/* Array stores values for each world camera, ordered left, right, far left, far right. Luminance estimate is in nits (cd/m^2). */
	UPROPERTY(BlueprintReadOnly, Category = "Light Estimation|MagicLeap")
	TArray<float> AmbientIntensityNits;

	UPROPERTY(BlueprintReadOnly, Category = "Light Estimation|MagicLeap")
	FTimespan Timestamp;
};

/**
	Information about the color temperature state.
	Capturing images or video will stop the lighting information update,
	causing the retrieved data to be stale (old timestamps).
*/
USTRUCT(BlueprintType)
struct FMagicLeapLightEstimationColorTemperatureState
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Light Estimation|MagicLeap")
	float ColorTemperatureKelvin;

	UPROPERTY(BlueprintReadOnly, Category = "Light Estimation|MagicLeap")
	FLinearColor AmbientColor;

	UPROPERTY(BlueprintReadOnly, Category = "Light Estimation|MagicLeap")
	FTimespan Timestamp;
};
