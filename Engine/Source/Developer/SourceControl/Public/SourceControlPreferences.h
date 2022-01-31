// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "SourceControlPreferences.generated.h"


/** Settings for the Source Control Integration */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Source Control"))
class SOURCECONTROL_API USourceControlPreferences : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Helper to access easily Enable Validation Tag setting */
	static bool IsValidationTagEnabled();

	/** Helper to access easily Should Delete New Files On Revert setting */
	static bool ShouldDeleteNewFilesOnRevert();

public:
	/** If enabled, adds a tag in changelist descriptions when they are validated */
	UPROPERTY(config, EditAnywhere, Category = SourceControl, meta = (ToolTip = "Adds validation tag to changelist description on submit."))
	bool bEnableValidationTag = true;

	/** If enabled, deletes new files when reverted. */
	UPROPERTY(config, EditAnywhere, Category = SourceControl, meta = (ToolTip = "Deletes new files when reverted."))
	bool bShouldDeleteNewFilesOnRevert = true;
};
