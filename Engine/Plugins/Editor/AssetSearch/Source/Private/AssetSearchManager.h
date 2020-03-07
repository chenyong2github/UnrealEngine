// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetSearchModule.h"
#include "AssetSearchDatabase.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"

class FRunnableThread;

class FAssetSearchManager : public FRunnable
{
public:
	FAssetSearchManager();
	~FAssetSearchManager();
	
	void Start();
	void RegisterIndexer(FName AssetClassName, IAssetIndexer* Indexer);

	FSearchStats GetStats() const;

	void Search(const FSearchQuery& Query, TFunction<void(TArray<FSearchRecord>&&)> InCallback);

	// Utility
	void ForceIndexOnAssetsMissingIndex();

private:
	bool Tick_GameThread(float DeltaTime);
	virtual uint32 Run() override;
	void Tick_DatabaseOperationThread();

private:
	void OnAssetAdded(const FAssetData& InAssetData);
	void OnObjectSaved(UObject* InObject);
	void OnAssetLoaded(UObject* InObject);
	void OnGetAssetTags(const UObject* Object, TArray<UObject::FAssetRegistryTag>& OutTags);
	void AddToContentTagCache(const FAssetData& InAsset, const FString& InContent, const FString& InContentHash);

	void AddOrUpdateAsset(const FAssetData& InAsset, const FString& IndexedJson, const FString& DerivedDataKey);

	void RequestIndexAsset(UObject* InAsset);
	bool IsAssetIndexable(UObject* InAsset);
	bool TryLoadIndexForAsset(const FAssetData& InAsset);
	FString TryGetDDCKeyForAsset(const FAssetData& InAsset);
	FString GetDerivedDataKey(const FSHAHash& IndexedContentHash);
	FString GetDerivedDataKey(const FAssetData& UnindexedAsset);
	bool HasIndexerForClass(UClass* AssetClass);
	void StoreIndexForAsset(UObject* InAsset, bool Unindexed);
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
	TAtomic<int32> PendingDownloads;
	TAtomic<int64> TotalSearchRecords;

	double LastRecordCountUpdateSeconds;

private:
	TMap<FName, IAssetIndexer*> Indexers;

	TArray<TWeakObjectPtr<UObject>> RequestIndexQueue;

	TArray<FAssetData> ProcessAssetQueue;

	struct FAssetDDCRequest
	{
		FAssetData AssetData;
		FString DDCKey_IndexDataHash;
		uint32 DDCHandle;
	};
	TQueue<FAssetDDCRequest, EQueueMode::Mpsc> ProcessDDCQueue;

	TArray<FAssetDDCRequest> FailedDDCRequests;

	FDelegateHandle TickerHandle;

private:
	TAtomic<bool> RunThread;
	FRunnableThread* DatabaseThread = nullptr;

	TQueue<TFunction<void()>> ImmediateOperations;
	TQueue<TFunction<void()>> FeedOperations;
	TQueue<TFunction<void()>> UpdateOperations;
};