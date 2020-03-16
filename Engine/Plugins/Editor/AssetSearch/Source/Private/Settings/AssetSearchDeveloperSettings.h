// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "AssetSearchDeveloperSettings.generated.h"

UCLASS(config = Editor, defaultconfig, meta=(DisplayName="Asset Search"))
class UAssetSearchDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAssetSearchDeveloperSettings();

	UPROPERTY(config, EditAnywhere, Category=General)
	TArray<FDirectoryPath> IgnoredPaths;
};
