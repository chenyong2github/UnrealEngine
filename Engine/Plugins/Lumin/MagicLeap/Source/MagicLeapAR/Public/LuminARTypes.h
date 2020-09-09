// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "MagicLeapHandle.h"
#include "MagicLeapImageTrackerTypes.h"

#include "ARTypes.h"
#include "ARTrackable.h"
#include "ARTraceResult.h"
#include "ARSystem.h"
#include "ARPin.h"
#include "ARLightEstimate.h"

#include "LuminARTypes.generated.h"

/// @defgroup LuminARBase
/// The base module for LuminAR plugin

/**
 * @ingroup LuminARBase
 * Describes the tracking state of the current ARCore session.
 */
UENUM(BlueprintType)
enum class ELuminARTrackingState : uint8
{
	/** Tracking is valid. */
	Tracking = 0,
	/** Tracking is temporary lost but could recover in the future. */
	NotTracking = 1,
	/** Tracking is lost will not recover. */
	StoppedTracking = 2
};

/**
 * @ingroup LuminARBase
 * Describes which channel ARLineTrace will be performed on.
 */
UENUM(BlueprintType, Category = "LuminAR|TraceChannel", meta = (Bitflags))
enum class ELuminARLineTraceChannel : uint8
{
	None = 0,
	/** Trace against feature point cloud. */
	FeaturePoint = 1,
	/** Trace against the infinite plane. */
	InfinitePlane = 2,
	/** Trace against the plane using its extent. */
	PlaneUsingExtent = 4,
	/** Trace against the plane using its boundary polygon. */
	PlaneUsingBoundaryPolygon = 8,
	/**
	 * Trace against feature point and attempt to estimate the normal of the surface centered around the trace hit point.
	 * Surface normal estimation is most likely to succeed on textured surfaces and with camera motion.
	 */
	FeaturePointWithSurfaceNormal = 16,
};
ENUM_CLASS_FLAGS(ELuminARLineTraceChannel);

UCLASS(BlueprintType, Category = "AR AugmentedReality|Light Estimation")
class MAGICLEAPAR_API ULuminARLightEstimate : public UARBasicLightEstimate
{
	GENERATED_BODY()

public:
	using UARBasicLightEstimate::SetLightEstimate;

	void SetLightEstimate(TArray<float> InAmbientIntensityNits, float InColorTemperatureKelvin, FLinearColor InAmbientColor)
	{
		SetLightEstimate(InColorTemperatureKelvin, InAmbientColor);
		AmbientIntensityNits = InAmbientIntensityNits;
	}

	UFUNCTION(BlueprintPure, Category = "AR AugmentedReality|Light Estimation")
	TArray<float> GetAmbientIntensityNits() const { return AmbientIntensityNits; }

private:
	UPROPERTY()
	TArray<float> AmbientIntensityNits;
};

UCLASS(BlueprintType, Category = "Lumin AR Candidate Image")
class MAGICLEAPAR_API ULuminARCandidateImage : public UARCandidateImage
{
	GENERATED_BODY()

public:
	static ULuminARCandidateImage* CreateNewLuminARCandidateImage(UTexture2D* InCandidateTexture, FString InFriendlyName, float InPhysicalWidth, float InPhysicalHeight, EARCandidateImageOrientation InOrientation, bool bInUseUnreliablePose, bool bInImageIsStationary, EMagicLeapImageTargetOrientation InAxisOrientation)
	{
		ULuminARCandidateImage* NewARCandidateImage = NewObject<ULuminARCandidateImage>();
		NewARCandidateImage->CandidateTexture = InCandidateTexture;
		NewARCandidateImage->FriendlyName = InFriendlyName;
		NewARCandidateImage->Width = InPhysicalWidth;
		NewARCandidateImage->Height = InPhysicalHeight;
		NewARCandidateImage->Orientation = InOrientation;
		NewARCandidateImage->bUseUnreliablePose = bInUseUnreliablePose;
		NewARCandidateImage->bImageIsStationary = bInImageIsStationary;
		NewARCandidateImage->AxisOrientation = InAxisOrientation;

		return NewARCandidateImage;
	}

	UFUNCTION(BlueprintPure, Category = "Lumin AR Candidate Image")
	bool GetUseUnreliablePose() const { return bUseUnreliablePose; }

	/** @see FriendlyName */
	UFUNCTION(BlueprintPure, Category = "Lumin AR Candidate Image")
	bool GetImageIsStationary() const { return bImageIsStationary; }

	UFUNCTION(BlueprintPure, Category = "Lumin AR Candidate Image")
	EMagicLeapImageTargetOrientation GetAxisOrientation() const { return AxisOrientation; }

private:
	UPROPERTY(EditAnywhere, Category="Lumin AR Candidate Image")
	bool bUseUnreliablePose;

	UPROPERTY(EditAnywhere, Category="Lumin AR Candidate Image")
	bool bImageIsStationary;

	UPROPERTY(EditAnywhere, Category="Lumin AR Candidate Image")
	EMagicLeapImageTargetOrientation AxisOrientation;
};
