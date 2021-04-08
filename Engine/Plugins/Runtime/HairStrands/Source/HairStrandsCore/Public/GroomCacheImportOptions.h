// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorFramework/AssetImportData.h"
#include "UObject/SoftObjectPath.h"
#include "GroomCacheImportOptions.generated.h"

USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FGroomCacheImportSettings
{
	GENERATED_BODY()

	/** Import the animated groom that was detected in this file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GroomCache)
	bool bImportGroomCache = true;

	/** Import or re-import the groom asset from this file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GroomCache)
	bool bImportGroomAsset = true;

	/** The groom asset the groom cache will be built from (must be compatible) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GroomCache, meta = (MetaClass = "GroomAsset"))
	FSoftObjectPath GroomAsset;
};

UCLASS(BlueprintType)
class HAIRSTRANDSCORE_API UGroomCacheImportOptions : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ShowOnlyInnerProperties), Category = GroomCache)
	FGroomCacheImportSettings ImportSettings;
};

/** The asset import data to store the import settings within the GroomCache asset */
UCLASS()
class HAIRSTRANDSCORE_API UGroomCacheImportData : public UAssetImportData
{
	GENERATED_BODY()

public:

	UPROPERTY()
	FGroomCacheImportSettings Settings;
};
