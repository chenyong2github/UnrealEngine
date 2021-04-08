// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDStageOptions.generated.h"

UENUM( BlueprintType )
enum class EUsdUpAxis : uint8
{
	YAxis,
	ZAxis,
};

USTRUCT( BlueprintType )
struct USDCLASSES_API FUsdStageOptions
{
	GENERATED_BODY()

	/** MetersPerUnit to use for the stage. Defaults to 0.01 (i.e. 1cm per unit, which equals UE units) */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options" )
	float MetersPerUnit = 0.01f;

	/** UpAxis to use for the stage. Defaults to ZAxis, which equals the UE convention */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options" )
	EUsdUpAxis UpAxis = EUsdUpAxis::ZAxis;
};
