// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetConnectionConfig.h"
#include "VCamWidgetConnectionState.generated.h"

struct FVCamConnectionTargetSettings;

USTRUCT()
struct VCAMCORE_API FVCamWidgetConnectionState
{
	GENERATED_BODY()

	/** A list of widgets to update */
	UPROPERTY(EditAnywhere, Category = "Connection")
	TArray<FWidgetConnectionConfig> WidgetConfigs;
};