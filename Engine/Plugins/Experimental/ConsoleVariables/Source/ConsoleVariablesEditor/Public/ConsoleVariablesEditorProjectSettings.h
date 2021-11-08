// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConsoleVariablesEditorProjectSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class CONSOLEVARIABLESEDITOR_API UConsoleVariablesEditorProjectSettings : public UObject
{
	GENERATED_BODY()
public:
	
	UConsoleVariablesEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
	{
		bAddAllChangedConsoleVariablesToCurrentPreset = true;
	}

	/** If true, any console variable changes will be added to the current preset as long as the plugin is loaded. */
	UPROPERTY(Config, EditAnywhere, Category="Console Variables")
	bool bAddAllChangedConsoleVariablesToCurrentPreset;
};
