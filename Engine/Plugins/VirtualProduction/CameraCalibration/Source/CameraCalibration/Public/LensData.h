// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Engine/EngineTypes.h"
#include "Models/LensModel.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LensData.generated.h"

USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FEncoderPoint
{
	GENERATED_BODY()

public:

	//Homed value in the range of [0..1]
	UPROPERTY(EditAnywhere, Category = "Point")
	float NormalizedValue = 0.0f;

	/*
	 * Converted value in physical units
	 * FIZ units
	 * F: cm
	 * I: F-Stops
	 * Z: mm
	 */
	UPROPERTY(EditAnywhere, Category = "Point")
	float ValueInPhysicalUnits = 0.0f;
};

/**
 * Information about the lens rig
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FLensInfo
{
	GENERATED_BODY()

public:

	/** Model name of the lens */
	UPROPERTY(EditAnywhere, Category = "Lens Info")
	FString LensModelName;

	/** Serial number of the lens */
	UPROPERTY(EditAnywhere, Category = "Lens Info")
	FString LensSerialNumber;

	/** Model of the lens (spherical, anamorphic, etc...) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens Info")
	TSubclassOf<ULensModel> LensModel;

	/** Width and height of the calibrated camera's sensor, in millimeters */
	UPROPERTY(EditAnywhere, Category = "Lens Info")
	FVector2D SensorDimensions = FVector2D(23.76f, 13.365f);
};

/**
 * Lens distortion parameters
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FDistortionInfo
{
	GENERATED_BODY()

public:
	/** Generic array of floating-point lens distortion parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	TArray<float> Parameters;
};

/**
 * Lens camera parameters
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FIntrinsicParameters
{
	GENERATED_BODY()

public:

	// Value expected to be normalized (unitless)
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector2D FxFy = FVector2D(1.0f, (16.0f / 9.0f));

	// Value expected to be normalized [0,1]
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (DisplayName = "Image Center"))
	FVector2D PrincipalPoint = FVector2D(0.5f, 0.5f);
};

/**
 * Lens nodal point offset
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATION_API FNodalPointOffset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Nodal point")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Nodal point")
	FQuat RotationOffset = FQuat::Identity;
};


