// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"

#include "USDProjectSettings.generated.h"

UCLASS(config=Editor, meta=(DisplayName=USDImporter), MinimalAPI)
class UUsdProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Additional paths to check for USD plugins
	UPROPERTY( config, EditAnywhere, Category = USD )
	TArray<FDirectoryPath> AdditionalPluginDirectories;

	UPROPERTY( config, EditAnywhere, Category = USD )
	bool bShowConfirmationWhenClearingLayers = true;

	// Whether to show the warning dialog when authoring opinions that could have no effect on the composed stage
	UPROPERTY( config, EditAnywhere, Category = USD )
	bool bShowOverriddenOpinionsWarning = true;
};