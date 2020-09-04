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
	UPROPERTY(config, EditAnywhere, Category=General)
	TArray<FDirectoryPath> AdditionalPluginDirectories;
};