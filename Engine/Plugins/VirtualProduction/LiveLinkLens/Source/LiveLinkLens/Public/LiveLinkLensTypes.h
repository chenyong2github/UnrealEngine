// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkCameraTypes.h"

#include "LensData.h"

#include "LiveLinkLensTypes.generated.h"

/**
 * Struct for static lens data
 */
USTRUCT(BlueprintType)
struct LIVELINKLENS_API FLiveLinkLensStaticData : public FLiveLinkCameraStaticData
{
	GENERATED_BODY()

	/** Specifies the type/character of the lens (spherical, anamorphic, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	ELensModel LensModel;
};

/**
 * Struct for dynamic (per-frame) lens data
 */
USTRUCT(BlueprintType)
struct LIVELINKLENS_API FLiveLinkLensFrameData : public FLiveLinkCameraFrameData
{
	GENERATED_BODY()

	/** Coefficients of the distortion model */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	FDistortionParameters DistortionParameters;

	/** Normalized center of the image, in the range [0.0f, 1.0f] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	FVector2D PrincipalPoint;
};

/**
 * Facility structure to handle lens data in blueprint
 */
USTRUCT(BlueprintType)
struct LIVELINKLENS_API FLiveLinkLensBlueprintData : public FLiveLinkBaseBlueprintData
{
	GENERATED_BODY()

	/** Static data that should not change every frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	FLiveLinkLensStaticData StaticData;

	/** Dynamic data that can change every frame  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	FLiveLinkLensFrameData FrameData;
};
