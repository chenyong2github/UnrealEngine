// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "DataLayerModeSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings)
class UDataLayerModeSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config)
	uint32 bShowDataLayerContent : 1;
};
