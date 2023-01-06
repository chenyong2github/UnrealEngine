// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/OutputDeviceRedirector.h"


class IMemoryUsageInfoProvider
{
public:
	virtual uint64 GetAssetMemoryUsage(FName Asset, FOutputDevice* ErrorOutput) const = 0;
	virtual uint64 GetAssetsMemoryUsage(const TSet<FName>& Assets, FOutputDevice* ErrorOutput) const = 0;
	virtual void GetAllAssetsWithSize(TMap<FName, uint64>& OutAssets, FOutputDevice* ErrorOutput) const = 0;
};

class FMemoryUsageInfoProviderLLM : public IMemoryUsageInfoProvider
{
public:
	virtual uint64 GetAssetMemoryUsage(FName Asset, FOutputDevice* ErrorOutput = GLog) const override;
	virtual uint64 GetAssetsMemoryUsage(const TSet<FName>& Assets, FOutputDevice* ErrorOutput = GLog) const override;
	virtual void GetAllAssetsWithSize(TMap<FName, uint64>& OutAssets, FOutputDevice* ErrorOutput = GLog) const override;

#if ENABLE_LOW_LEVEL_MEM_TRACKER
	virtual void GetFilteredTagsWithSize(TMap<FName, uint64>& OutTags, ELLMTracker Tracker, ELLMTagSet TagSet, TArray<FLLMTagSetAllocationFilter>& Filters, FOutputDevice* ErrorOutput = GLog) const;
#endif
};