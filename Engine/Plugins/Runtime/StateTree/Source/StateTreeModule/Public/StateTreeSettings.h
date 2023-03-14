// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "StateTreeSettings.generated.h"

/**
 * Default StateTree settings
 */
UCLASS(config = StateTree, defaultconfig, DisplayName = "StateTree")
class STATETREEMODULE_API UStateTreeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static UStateTreeSettings& Get() { return *CastChecked<UStateTreeSettings>(UStateTreeSettings::StaticClass()->GetDefaultObject()); }
	
	UPROPERTY(EditDefaultsOnly, Category = StateTree, config)
	bool bUseDebugger = false;
};