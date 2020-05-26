// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"

#include "USDAssetImportData.generated.h"

UCLASS(config = EditorPerProjectUserSettings, AutoExpandCategories = (Options), MinimalAPI)
class UUsdAssetImportData : public UAssetImportData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString PrimPath;

	// Likely a UUSDStageImportOptions, but we don't declare it here
	// to prevent an unnecessary module dependency on USDStageImporter
	UPROPERTY()
	class UObject* ImportOptions;
};