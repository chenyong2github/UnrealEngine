// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Engine/EngineTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LensData.generated.h"

UENUM(BlueprintType)
enum class ELensModel : uint8
{
	Spherical = 0 UMETA(DisplayName = "Spherical")
};

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

	/** Model name of the lens */
	UPROPERTY(EditAnywhere, Category = "Lens Info")
	FString LensModelName;

	/** Serial number of the lens */
	UPROPERTY(EditAnywhere, Category = "Lens Info")
	FString LensSerialNumber;

	/** Model of the lens (spherical, anamorphic, etc...) */
	UPROPERTY(EditAnywhere, Category = "Lens Info")
	ELensModel LensModel = ELensModel::Spherical;
};

/**
 * Lens distortion parameters
 */
USTRUCT(BlueprintType)
struct LENSDISTORTION_API FDistortionParameters
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	float K1 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	float K2 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	float K3 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	float P1 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	float P2 = 0.0f;

public:
	bool operator==(const FDistortionParameters& Other) const
	{
		return ((K1 == Other.K1)
			&& (K2 == Other.K2)
			&& (K3 == Other.K3)
			&& (P1 == Other.P1)
			&& (P2 == Other.P2));
	}
	bool operator!=(const FDistortionParameters& Other) const 
	{
		return !(*this == Other);
	}
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


