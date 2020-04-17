// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Async/Future.h"
#include "Async/AsyncResult.h"
#include "SearchSerializer.h"
#include "Modules/ModuleManager.h"

class UClass;

struct FSearchQuery
{
	FString Query;

	FString ConvertToDatabaseQuery() const;
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
	int32 Processing = 0;
	int32 Updating = 0;

	int32 AssetsMissingIndex = 0;

	int64 TotalRecords = 0;

	bool IsUpdating() const
	{
		return (Scanning + Processing + Updating) > 0;
	}
};

class IAssetIndexer
{
public:

	virtual ~IAssetIndexer() = 0;
	virtual FString GetName() const = 0;
	virtual int32 GetVersion() const = 0;
	virtual void IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer) const = 0;

	/**
	 * If your package contains a nested asset, such as the Blueprint stored in Level/World packages, 
	 * it would return UBlueprint's class in the array.  This is only important if you use IndexNestedAsset.
	 */
	virtual void GetNestedAssetTypes(TArray<UClass*>& OutTypes) const { }
};

inline IAssetIndexer::~IAssetIndexer() = default;

class UClass;

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

	virtual void RegisterAssetIndexer(const UClass* AssetClass, TUniquePtr<IAssetIndexer>&& Indexer) = 0;

public:

	/** Virtual destructor. */
	virtual ~IAssetSearchModule() { }
};
