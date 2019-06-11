// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkTypes.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "LiveLinkCameraTypes.generated.h"

UENUM()
enum class ELiveLinkCameraProjectionMode : uint8
{
	Perspective,
	Orthographic
};

/**
 * Static data for Camera data. 
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkCameraStaticData : public FLiveLinkTransformStaticData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	bool bIsFieldOfViewSupported = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	bool bIsAspectRatioSupported = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	bool bIsFocalLengthSupported = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	bool bIsProjectionModeSupported = false;
};

/**
 * Dynamic data for camera 
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkCameraFrameData : public FLiveLinkTransformFrameData
{
	GENERATED_BODY()

	// Field of View of the camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	float FieldOfView = 90.f;

	// Aspect Ratio of the camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	float AspectRatio = 1.777778f;

	// Focal length of the camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	float FocalLength = 50.f;

	// Focal length of the camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	ELiveLinkCameraProjectionMode ProjectionMode = ELiveLinkCameraProjectionMode::Perspective;
};

/**
 * Facility structure to handle camera data in blueprint
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkCameraBlueprintData : public FLiveLinkBaseBlueprintData
{
	GENERATED_BODY()
	
	// Static data that should not change every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	FLiveLinkCameraStaticData StaticData;

	// Dynamic data that can change every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	FLiveLinkCameraFrameData FrameData;
};
