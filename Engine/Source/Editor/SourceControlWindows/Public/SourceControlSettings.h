// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "SourceControlSettings.generated.h"


/** Settings for the Source Control Integration */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Source Control"))
class USourceControlSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Helper to access easily Enable Validation Tag setting */
	static bool IsValidationTagEnabled();

public:
	/** If enabled, adds a tag in changelist descriptions when they are validated */
	UPROPERTY(config, EditAnywhere, Category = SourceControl, meta = (ToolTip = "Adds validation tag to changelist description on submit."))
	bool bEnableValidationTag = true;
};
