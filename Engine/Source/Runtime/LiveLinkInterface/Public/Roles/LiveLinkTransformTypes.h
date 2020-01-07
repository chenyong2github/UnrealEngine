// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkTypes.h"
#include "LiveLinkTransformTypes.generated.h"

/**
 * Static data for Transform data. 
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkTransformStaticData : public FLiveLinkBaseStaticData
{
	GENERATED_BODY()
};

/**
 * Dynamic data for Transform 
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkTransformFrameData : public FLiveLinkBaseFrameData
{
	GENERATED_BODY()

	// Transform of the frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Properties", Interp)
	FTransform Transform;
};

/**
 * Facility structure to handle transform data in blueprint
 */
USTRUCT(BlueprintType)
struct LIVELINKINTERFACE_API FLiveLinkTransformBlueprintData : public FLiveLinkBaseBlueprintData
{
	GENERATED_BODY()

	// Static data that should not change every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform Data")
	FLiveLinkTransformStaticData StaticData;

	// Dynamic data that can change every frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform Data")
	FLiveLinkTransformFrameData FrameData;
};
