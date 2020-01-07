// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/IChunkDataGenerator.h"

class FLocTextHelper;

/**
 * Implementation for splitting localization data into chunks when creating streaming install manifests.
 */
class FLocalizationChunkDataGenerator : public IChunkDataGenerator
{
public:
	FLocalizationChunkDataGenerator(TArray<FString> InLocalizationTargetsToChunk, TArray<FString> InAllCulturesToCook);
	virtual ~FLocalizationChunkDataGenerator() = default;

	//~ IChunkDataGenerator
	virtual void GenerateChunkDataFiles(const int32 InChunkId, const TSet<FName>& InPackagesInChunk, const FString& InPlatformName, FSandboxPlatformFile* InSandboxFile, TArray<FString>& OutChunkFilenames) override;

private:
	/** Update CachedLocalizationTargetData if needed */
	void ConditionalCacheLocalizationTargetData();

	/** List of localization targets that should be chunked */
	TArray<FString> LocalizationTargetsToChunk;

	/** Complete list of cultures to cook data for, including inferred parent cultures */
	TArray<FString> AllCulturesToCook;

	/** Cached localization target helpers, to avoid redundant work for each chunk */
	TArray<TSharedPtr<FLocTextHelper>> CachedLocalizationTargetHelpers;
};
