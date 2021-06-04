// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Engine/EngineTypes.h"
#include "Engine/Texture.h"
#include "Models/LensModel.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LensData.generated.h"


/**
 * Information about the lens rig
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FLensInfo
{
	GENERATED_BODY()

public:
	bool operator==(const FLensInfo& Other) const
	{
		return LensModelName == Other.LensModelName
			&& LensSerialNumber == Other.LensSerialNumber
			&& LensModel == Other.LensModel
			&& SensorDimensions == Other.SensorDimensions;
	}
	
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
struct CAMERACALIBRATIONCORE_API FDistortionInfo
{
	GENERATED_BODY()

public:
	/** Generic array of floating-point lens distortion parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	TArray<float> Parameters;
};

/**
 * Normalized focal length information for both width and height dimension
 * If focal length is in pixel, normalize using pixel dimensions
 * If focal length is in mm, normalize using sensor dimensions
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FFocalLengthInfo
{
	GENERATED_BODY()

public:

	/** Value expected to be normalized (unitless) */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector2D FxFy = FVector2D(1.0f, (16.0f / 9.0f));
};

/**
 * Pre generate STMap and normalized focal length information
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FSTMapInfo
{
	GENERATED_BODY()

public:
	/** 
	 * Pre calibrated UVMap/STMap
	 * RG channels are expected to have undistortion map (from distorted to undistorted)
	 * BA channels are expected to have distortion map (from undistorted (CG) to distorted)
	 */
	UPROPERTY(EditAnywhere, Category = "Distortion")
	UTexture* DistortionMap = nullptr;
};

/**
 * Lens camera image center parameters
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FImageCenterInfo
{
	GENERATED_BODY()

public:
	/** Value expected to be normalized [0,1] */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (DisplayName = "Image Center"))
	FVector2D PrincipalPoint = FVector2D(0.5f, 0.5f);
};

/**
 * Lens nodal point offset
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FNodalPointOffset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Nodal point")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Nodal point")
	FQuat RotationOffset = FQuat::Identity;
};

/**
* Distortion data evaluated for given FZ pair based on lens parameters
*/
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FDistortionData
{
	GENERATED_BODY()

	public:

	UPROPERTY(VisibleAnywhere, Category = "Distortion")
	TArray<FVector2D> DistortedUVs;

	/** Estimated overscan factor based on distortion to have distorted cg covering full size */
	UPROPERTY(EditAnywhere, Category = "Distortion")
	float OverscanFactor = 1.0f;
};


