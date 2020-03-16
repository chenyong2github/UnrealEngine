// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "SearchUserSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings, meta=(DisplayName="Search"))
class USearchUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USearchUserSettings();

	/** Enable this to begin using search. */
	UPROPERTY(config, EditAnywhere, Category = General)
	bool bEnableSearch;

	UPROPERTY(config, EditAnywhere, Category=General)
	TArray<FDirectoryPath> IgnoredPaths;

	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category=General)
	bool bShowAdvancedData;
};
