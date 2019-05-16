// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LandscapeSettings.generated.h"

UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "Landscape"))
class LANDSCAPE_API ULandscapeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Layers", meta=(UIMin = "1", UIMax = "32", ClampMin = "1", ClampMax = "32", ToolTip = "This option controls the maximum editing layers that can be added to a Landscape"))
	int32 MaxNumberOfLayers = 8;
};