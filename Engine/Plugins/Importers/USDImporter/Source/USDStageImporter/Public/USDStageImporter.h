// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDStageActor.h"

#include "CoreMinimal.h"

class UUsdAssetImportData;
struct FUsdStageImportContext;

class USDSTAGEIMPORTER_API UUsdStageImporter
{
public:
	static UUsdAssetImportData* GetAssetImportData(UObject* Asset);

	void ImportFromFile(FUsdStageImportContext& ImportContext);

	bool ReimportSingleAsset(FUsdStageImportContext& ImportContext, UObject* OriginalAsset, UUsdAssetImportData* OriginalImportData, UObject*& OutReimportedAsset);
};
