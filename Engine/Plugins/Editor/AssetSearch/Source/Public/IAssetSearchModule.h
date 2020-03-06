// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Async/Future.h"
#include "Async/AsyncResult.h"
#include "SearchSerializer.h"
#include "Modules/ModuleManager.h"

struct FSearchQuery
{
	FString Query;
};

struct FSearchRecord
{
	FString AssetName;
	FString AssetPath;
	FString AssetClass;

	FString object_name;
	FString object_path;
	FString object_native_class;

	FString property_name;
	FString property_field;
	FString property_class;

	FString value_text;
	FString value_hidden;

	float Score;
};

struct FSearchStats
{
	int32 Scanning = 0;
	int32 Downloading = 0;
	int32 PendingDatabaseUpdates = 0;

	int32 AssetsMissingIndex = 0;

	int64 TotalRecords = 0;
};

class IAssetIndexer
{
public:
	virtual FString GetName() const = 0;
	virtual int32 GetVersion() const = 0;
	virtual void IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer) = 0;
};

/**
 *
 */
class IAssetSearchModule : public IModuleInterface
{
public:
	static inline IAssetSearchModule& Get()
	{
		static const FName ModuleName = "AssetSearch";
		return FModuleManager::LoadModuleChecked<IAssetSearchModule>(ModuleName);
	}

	static inline bool IsAvailable()
	{
		static const FName ModuleName = "AssetSearch";
		return FModuleManager::Get().IsModuleLoaded(ModuleName);
	}

	virtual FSearchStats GetStats() const = 0;

	virtual void Search(const FSearchQuery& Query, TFunction<void(TArray<FSearchRecord>&&)> InCallback) = 0;

	virtual void ForceIndexOnAssetsMissingIndex() = 0;

	virtual void RegisterIndexer(FName AssetClassName, IAssetIndexer* Indexer) = 0;
	virtual void UnregisterIndexer(IAssetIndexer* Indexer) = 0;

public:

	/** Virtual destructor. */
	virtual ~IAssetSearchModule() { }
};
