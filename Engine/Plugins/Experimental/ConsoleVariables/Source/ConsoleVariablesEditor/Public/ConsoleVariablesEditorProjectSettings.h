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
		bUseAwesomenessDetection = true;
	}

	UPROPERTY(EditAnywhere, Category="Console Variables")
	bool bUseAwesomenessDetection;
};
