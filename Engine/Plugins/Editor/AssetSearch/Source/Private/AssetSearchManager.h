// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAssetSearchModule.h"
#include "AssetSearchDatabase.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"

class FRunnableThread;
class UClass;

class FAssetSearchManager : public FRunnable
{
public:
	FAssetSearchManager();
	virtual ~FAssetSearchManager();
	
	void Start();
	void RegisterAssetIndexer(const UClass* AssetClass, TUniquePtr<IAssetIndexer>&& Indexer);

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
	void OnAssetRemoved(const FAssetData& InAssetData);
	void OnAssetScanFinished();

	void OnObjectSaved(UObject* InObject);
	void OnAssetLoaded(UObject* InObject);

	void AddOrUpdateAsset(const FAssetData& InAsset, const FString& IndexedJson, const FString& DerivedDataKey);

	bool RequestIndexAsset(UObject* InAsset);
	bool IsAssetIndexable(UObject* InAsset);
	bool TryLoadIndexForAsset(const FAssetData& InAsset);
	bool AsyncGetDerivedDataKey(const FAssetData& UnindexedAsset, TFunction<void(FString)> DDCKeyCallback);
	bool HasIndexerForClass(const UClass* InAssetClass) const;
	FString GetIndexerVersion(const UClass* InAssetClass) const;
	void StoreIndexForAsset(UObject* InAsset);
	void LoadDDCContentIntoDatabase(const FAssetData& InAsset, const TArray<uint8>& Content, const FString& DerivedDataKey);

	void AsyncMainThreadTask(TFunction<void()> Task);
	void ProcessGameThreadTasks();

private:
	FAssetSearchDatabase SearchDatabase;
	FCriticalSection SearchDatabaseCS;
	TAtomic<int32> PendingDatabaseUpdates;
	TAtomic<int32> PendingDownloads;
	TAtomic<int64> TotalSearchRecords;

	double LastRecordCountUpdateSeconds;

private:
	TMap<FName, TUniquePtr<IAssetIndexer>> Indexers;

	TArray<TWeakObjectPtr<UObject>> RequestIndexQueue;

	struct FAssetOperation
	{
		FAssetData Asset;
		bool bRemoval = false;
	};

	TArray<FAssetOperation> ProcessAssetQueue;

	struct FAssetDDCRequest
	{
		FAssetData AssetData;
		FString DDCKey_IndexDataHash;
		uint32 DDCHandle;
	};
	TQueue<FAssetDDCRequest, EQueueMode::Mpsc> ProcessDDCQueue;

	TArray<FAssetDDCRequest> FailedDDCRequests;

	FDelegateHandle TickerHandle;

	TQueue<TFunction<void()>> GT_Tasks;

private:
	TAtomic<bool> RunThread;
	FRunnableThread* DatabaseThread = nullptr;

	TQueue<TFunction<void()>> ImmediateOperations;
	TQueue<TFunction<void()>> FeedOperations;
	TQueue<TFunction<void()>> UpdateOperations;
};