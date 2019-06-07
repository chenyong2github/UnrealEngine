// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkTransformTypes.h"
#include "LiveLinkLightTypes.generated.h"

/**
 * Static data for Light data. 
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkLightStaticData : public FLiveLinkTransformStaticData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	bool bIsTemperatureSupported = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	bool bIsIntensitySupported = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	bool bIsLightColorSupported = false;
};

/**
 * Dynamic data for light. 
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkLightFrameData : public FLiveLinkTransformFrameData
{
	GENERATED_BODY()

	// Color temperature in Kelvin of the blackbody illuminant
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	float Temperature = 6500.f;

	// Total energy that the light emits.  
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	float Intensity = 3.1415926535897932f;

	/** Filter color of the light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink", Interp)
	FColor LightColor = FColor::White;
};

/**
 * Facility structure to handle light data in blueprint
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkLightBlueprintData : public FLiveLinkBaseBlueprintData
{
	GENERATED_BODY()
	
	// Static data that should not change every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	FLiveLinkLightStaticData StaticData;

	// Dynamic data that can change every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveLink")
	FLiveLinkLightFrameData FrameData;
};
