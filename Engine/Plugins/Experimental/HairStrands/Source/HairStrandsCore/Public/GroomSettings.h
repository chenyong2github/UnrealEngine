// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GroomSettings.generated.h"

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FGroomConversionSettings
{
	GENERATED_USTRUCT_BODY()

		FGroomConversionSettings()
		: Rotation(FVector::ZeroVector)
		, Scale(FVector(1.0f, 1.0f, 1.0f))
	{}

	/** Rotation in Euler angles in degrees to fix up or front axes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conversion)
		FVector Rotation;

	/** Scale value to convert file unit into centimeters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Conversion)
		FVector Scale;
};

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FGroomBuildSettings
{
	GENERATED_USTRUCT_BODY()

	FGroomBuildSettings()
		: bOverrideGuides(false)
		, HairToGuideDensity(0.1f)
	{}

	/** Flag to override the imported guides with generated guides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BuildSettings", meta = (ToolTip = "If checked, override imported guides with generated ones."))
	bool bOverrideGuides;

	/** Density factor for converting hair into guide curve if no guides are provided. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BuildSettings", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "1.0"))
	float HairToGuideDensity;
};
