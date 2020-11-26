// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "WaterCurveSettings.generated.h"

class UCurveFloat;

USTRUCT(BlueprintType)
struct FWaterCurveSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Water)
	bool bUseCurveChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Water)
	UCurveFloat* ElevationCurveAsset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Water)
	float ChannelEdgeOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Water)
	float ChannelDepth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Water)
	float CurveRampWidth;
};