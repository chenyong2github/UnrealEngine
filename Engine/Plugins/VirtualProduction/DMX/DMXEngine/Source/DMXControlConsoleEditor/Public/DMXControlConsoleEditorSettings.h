// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DMXControlConsoleEditorSettings.generated.h"


/** Settings that holds DMX Control Console configurations. */
UCLASS(Config = DMXControlConsoleEditor, DefaultConfig, meta = (DisplayName = "DMXControlConsoleEditor"))
class UDMXControlConsoleEditorSettings
	: public UObject
{
	GENERATED_BODY()

public:
	/** Path to the current control console preset */
	UPROPERTY(Config)
	FSoftObjectPath ControlConsolePreset;
};
