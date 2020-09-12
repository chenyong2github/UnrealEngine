// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXAttribute.h"
#include "DMXPixelMappingExtraAttribute.generated.h"


/**
 * Struct for exposing and setting values to extra arguments
 */
USTRUCT(BlueprintType, Blueprintable)
struct DMXPIXELMAPPINGRUNTIME_API FDMXPixelMappingExtraAttribute
{
	GENERATED_BODY()

		FDMXPixelMappingExtraAttribute()
		: Value(0)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings")
	FDMXAttributeName Attribute;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (ClampMin = "0", UIMin = "0", ClampMax = "4294967295", UIMax = "4294967295"))
	int64 Value;
};
