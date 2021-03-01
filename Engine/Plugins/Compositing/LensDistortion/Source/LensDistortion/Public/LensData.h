// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Engine/EngineTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LensData.generated.h"


USTRUCT(BlueprintType)
struct LENSDISTORTION_API FEncoderPoint
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
struct LENSDISTORTION_API FLensInfo
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Lens Info")
	FString LensModel;

	UPROPERTY(EditAnywhere, Category = "Lens Info")
	FString LensSerialNumber;
};

/**
 * Lens distortion parameters
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FDistortionParameters
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float K1 = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float K2 = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float K3 = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float P1 = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Distortion")
	float P2 = 0.0f;
};

/**
 * Lens camera parameters
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FIntrinsicParameters
{
	GENERATED_BODY()

public:

	// Value expected to be in mm
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector2D FocalLength = FVector2D::ZeroVector;

	// Value expected to be normalized [0,1]
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector2D CenterShift = FVector2D(0.5f, 0.5f);
};

/**
 * Lens nodal point offset
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FNodalPointOffset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Nodal point")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Nodal point")
	FQuat RotationOffset = FQuat::Identity;
};


