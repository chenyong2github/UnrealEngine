// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetSearchModule.h"
#include "AssetSearchDatabase.h"

class FRunnableThread;

class FAssetSearchManager
{
public:
	FAssetSearchManager();
	~FAssetSearchManager();
	
	void Start();
	void RegisterIndexer(FName AssetClassName, IAssetIndexer* Indexer);

	FSearchStats GetStats() const;

	bool Search(const FSearchQuery& Query, TFunctionRef<bool(FSearchRecord&&)> InCallback);

private:
	bool Tick_GameThread(float DeltaTime);

private:
	void OnAssetAdded(const FAssetData& InAssetData);
	void OnObjectSaved(UObject* InObject);
	void OnAssetLoaded(UObject* InObject);
	void OnGetAssetTags(const UObject* Object, TArray<UObject::FAssetRegistryTag>& OutTags);
	void AddToContentTagCache(const FAssetData& InAsset, const FString& InContent, const FString& InContentHash);

	void AddOrUpdateAsset(const FAssetData& InAsset, const FString& IndexedJson, const FString& DerivedDataKey);

	void RequestIndexAsset(UObject* InAsset);
	bool IsAssetIndexable(UObject* InAsset);
	void TryLoadIndexForAsset(const FAssetData& InAsset);
	FString TryGetDDCKeyForAsset(const FAssetData& InAsset);
	FString GetDerivedDataKey(const FSHAHash& IndexedContentHash);
	FString GetDerivedDataKey(const FAssetData& UnindexedAsset);
	bool HasIndexerForClass(UClass* AssetClass);
	void StoreIndexForAsset(UObject* InAsset, bool Unindexed = false);
	void LoadDDCContentIntoDatabase(const FAssetData& InAsset, const TArray<uint8>& Content, const FString& DerivedDataKey);

private:
	struct FContentHashEntry
	{
		FString ContentHash;
		FString Content;
	};
	TMap<FString /*AssetPath*/, FContentHashEntry> ContentHashCache;

private:
	FAssetSearchDatabase SearchDatabase;
	FCriticalSection SearchDatabaseCS;
	TAtomic<int32> PendingDatabaseUpdates;

private:
	TMap<FName, IAssetIndexer*> Indexers;

	TArray<FAssetData> ProcessAssetQueue;

	struct FAssetDDCRequest
	{
		FAssetData AssetData;
		FString DDCKey_IndexDataHash;
		uint32 DDCHandle;
	};
	TArray<FAssetDDCRequest> ProcessDDCQueue;

	FDelegateHandle TickerHandle;

private:
	FRunnableThread* Thread;
};