// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetSearchManager.h"
#include "IAssetRegistry.h"
#include "AssetRegistryModule.h"
#include "AssetSearchDatabase.h"
#include "Async/Async.h"
#include "DerivedDataCacheInterface.h"

#include "Indexers/DataAssetIndexer.h"
#include "Indexers/DataTableIndexer.h"
#include "Indexers/BlueprintIndexer.h"
#include "Indexers/WidgetBlueprintIndexer.h"
#include "Containers/StringConv.h"
#include "Containers/Ticker.h"
#include "Misc/Paths.h"
#include "HAL/RunnableThread.h"

static bool bIndexUnindexAssetsOnLoad = false;
FAutoConsoleVariableRef CVarIndexUnindexAssetsOnLoad(
	TEXT("Search.IndexUnindexAssetsOnLoad"),
	bIndexUnindexAssetsOnLoad,
	TEXT("Index Unindex Assets On Load")
);

static int32 PendingDownloadsMax = 100;
FAutoConsoleVariableRef CVarPendingDownloadsMax(
	TEXT("Search.PendingDownloadsMax"),
	PendingDownloadsMax,
	TEXT("")
);

static int32 GameThread_DownloadProcessLimit = 30;
FAutoConsoleVariableRef CVarGameThread_DownloadProcessLimit(
	TEXT("Search.GameThread_DownloadProcessLimit"),
	GameThread_DownloadProcessLimit,
	TEXT("")
);

static int32 GameThread_AssetScanLimit = 1000;
FAutoConsoleVariableRef CVarGameThread_AssetScanLimit(
	TEXT("Search.GameThread_AssetScanLimit"),
	GameThread_AssetScanLimit,
	TEXT("")
);

FAssetSearchManager::FAssetSearchManager()
{
	PendingDatabaseUpdates = 0;
	PendingDownloads = 0;
	TotalSearchRecords = 0;
	LastRecordCountUpdateSeconds = 0;

	RunThread = false;
}

FAssetSearchManager::~FAssetSearchManager()
{
	RunThread = false;

	{
		FScopeLock ScopedLock(&SearchDatabaseCS);
		FCoreUObjectDelegates::OnObjectSaved.RemoveAll(this);
		FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
		UObject::FAssetRegistryTag::OnGetExtraObjectTags.RemoveAll(this);

		//FModuleManager::GetModule<FAssetRegistryModule>("AssetRegistry");

		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}
}

void FAssetSearchManager::Start()
{
	RegisterIndexer(TEXT("DataAsset"), new FDataAssetIndexer());
	RegisterIndexer(TEXT("DataTable"), new FDataTableIndexer());
	RegisterIndexer(TEXT("Blueprint"), new FBlueprintIndexer());
	RegisterIndexer(TEXT("WidgetBlueprint"), new FWidgetBlueprintIndexer());

	const FString SessionPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Search")));
	SearchDatabase.Open(SessionPath);

	FCoreUObjectDelegates::OnObjectSaved.AddRaw(this, &FAssetSearchManager::OnObjectSaved);
	FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FAssetSearchManager::OnAssetLoaded);
	UObject::FAssetRegistryTag::OnGetExtraObjectTags.AddRaw(this, &FAssetSearchManager::OnGetAssetTags);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.OnAssetAdded().AddRaw(this, &FAssetSearchManager::OnAssetAdded);
	AssetRegistry.GetAllAssets(ProcessAssetQueue, true);

	TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAssetSearchManager::Tick_GameThread), 0);

	RunThread = true;
	DatabaseThread = FRunnableThread::Create(this, TEXT("UniversalSearch"), 0, TPri_BelowNormal);
}

FSearchStats FAssetSearchManager::GetStats() const
{
	FSearchStats Stats;
	Stats.Scanning = ProcessAssetQueue.Num();
	Stats.Downloading = PendingDownloads;
	Stats.PendingDatabaseUpdates = PendingDatabaseUpdates;
	Stats.TotalRecords = TotalSearchRecords;
	return Stats;
}

void FAssetSearchManager::RegisterIndexer(FName AssetClassName, IAssetIndexer* Indexer)
{
	check(IsInGameThread());
	Indexers.Add(AssetClassName, Indexer);
}

void FAssetSearchManager::OnAssetAdded(const FAssetData& InAssetData)
{
	check(IsInGameThread());
	ProcessAssetQueue.Add(InAssetData);
}

void FAssetSearchManager::OnObjectSaved(UObject* InObject)
{
	check(IsInGameThread());

	if (!GIsCookerLoadingPackage)
	{
		StoreIndexForAsset(InObject);
	}
}

void FAssetSearchManager::OnAssetLoaded(UObject* InObject)
{
	check(IsInGameThread());

	if (bIndexUnindexAssetsOnLoad)
	{
		RequestIndexAsset(InObject);
	}
}

void FAssetSearchManager::OnGetAssetTags(const UObject* Object, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	check(IsInGameThread());

	FString ObjectPath = Object->GetPathName();
	const FContentHashEntry* ContentEntry = ContentHashCache.Find(ObjectPath);

	if (ContentEntry)
	{
		static FName SearchIndexContentTag(TEXT("SearchIndexContent"));
		static FName SearchIndexContentKeyTag(TEXT("SearchIndexContentKey"));
		OutTags.Add(UObject::FAssetRegistryTag(SearchIndexContentKeyTag, *ContentEntry->ContentHash, UObject::FAssetRegistryTag::TT_Hidden));
		OutTags.Add(UObject::FAssetRegistryTag(SearchIndexContentTag, *ContentEntry->Content, UObject::FAssetRegistryTag::TT_Hidden));
	}
}

void FAssetSearchManager::AddToContentTagCache(const FAssetData& InAsset, const FString& InContent, const FString& InContentHash)
{
	FContentHashEntry Entry;
	Entry.Content = InContent;
	Entry.ContentHash = InContentHash;
	ContentHashCache.Add(InAsset.ObjectPath.ToString(), Entry);
}

void FAssetSearchManager::RequestIndexAsset(UObject* InAsset)
{
	TWeakObjectPtr<UObject> AssetWeakPtr = InAsset;

	FAssetData AssetData(InAsset);
	FString AssetJsonDDCKey = TryGetDDCKeyForAsset(InAsset);

	UpdateOperations.Enqueue([this, AssetData, AssetWeakPtr, AssetJsonDDCKey]() {
		FScopeLock ScopedLock(&SearchDatabaseCS);
		if (!SearchDatabase.IsAssetUpToDate(AssetData, AssetJsonDDCKey))
		{
			AsyncTask(ENamedThreads::GameThread, [this, AssetWeakPtr]() {
				StoreIndexForAsset(AssetWeakPtr.Get(), true);
			});
		}
	});
}

bool FAssetSearchManager::IsAssetIndexable(UObject* InAsset)
{
	if (InAsset && InAsset->IsAsset())
	{
		// If it's not a permanent package, and one we just loaded for diffing, don't index it.
		UPackage* Package = InAsset->GetOutermost();
		if (Package->HasAnyPackageFlags(LOAD_ForDiff | LOAD_PackageForPIE | LOAD_ForFileDiff))
		{
			return false;
		}

		if (InAsset->HasAnyFlags(RF_Transient))
		{
			return false;
		}

		return true;
	}

	return false;
}

FString FAssetSearchManager::TryGetDDCKeyForAsset(const FAssetData& InAsset)
{
	FString AssetJsonDDCKey;

	{
		FString ContentDDCKey;
		static FName SearchIndexContentKeyTag(TEXT("SearchIndexContentKey"));
		if (InAsset.GetTagValue(SearchIndexContentKeyTag, ContentDDCKey))
		{
			AssetJsonDDCKey = ContentDDCKey;
		}
		else
		{
			UClass* AssetClass = InAsset.GetClass();
			if (HasIndexerForClass(AssetClass))
			{
				const FString UnindexedAssetKey = GetDerivedDataKey(InAsset);
				AssetJsonDDCKey = UnindexedAssetKey;
			}
		}
	}

	return AssetJsonDDCKey;
}

bool FAssetSearchManager::TryLoadIndexForAsset(const FAssetData& InAsset)
{
	check(IsInGameThread());
	FString AssetJsonDDCKey = TryGetDDCKeyForAsset(InAsset);

	if (!AssetJsonDDCKey.IsEmpty())
	{
		PendingDownloads++;
		FeedOperations.Enqueue([this, InAsset, AssetJsonDDCKey]() {
			FScopeLock ScopedLock(&SearchDatabaseCS);
			if (!SearchDatabase.IsAssetUpToDate(InAsset, AssetJsonDDCKey))
			{
				FAssetDDCRequest DDCRequest;
				DDCRequest.AssetData = InAsset;
				DDCRequest.DDCKey_IndexDataHash = AssetJsonDDCKey;
				DDCRequest.DDCHandle = GetDerivedDataCacheRef().GetAsynchronous(*AssetJsonDDCKey, InAsset.ObjectPath.ToString());
				ProcessDDCQueue.Enqueue(DDCRequest);
			}
			else
			{
				PendingDownloads--;
			}
		});

		return true;
	}

	return false;
}

FString FAssetSearchManager::GetDerivedDataKey(const FSHAHash& IndexedContentHash)
{
	const FString DDCKey = TEXT("AssetSearch_B") + LexToString(FSearchSerializer::GetVersion()) + TEXT("_") + IndexedContentHash.ToString();
	return DDCKey;
}

FString FAssetSearchManager::GetDerivedDataKey(const FAssetData& UnindexedAsset)
{
	FString ContentPath = UnindexedAsset.ObjectPath.ToString();

	FSHAHash UnindexedAssetHash;
	FSHA1::HashBuffer(*ContentPath, ContentPath.Len() * sizeof(FString::ElementType), UnindexedAssetHash.Hash);

	const FString DDCKey = TEXT("AssetSearch_Legacy_B") + LexToString(FSearchSerializer::GetVersion()) + TEXT("_") + UnindexedAssetHash.ToString();
	return DDCKey;
}

bool FAssetSearchManager::HasIndexerForClass(UClass* AssetClass)
{
	UClass* IndexableClass = AssetClass;
	while (IndexableClass)
	{
		if (IAssetIndexer* Indexer = Indexers.FindRef(IndexableClass->GetFName()))
		{
			return true;
		}

		IndexableClass = IndexableClass->GetSuperClass();
	}

	return false;
}

void FAssetSearchManager::StoreIndexForAsset(UObject* InAsset, bool bLegacyIndexing)
{
	check(IsInGameThread());

	if (IsAssetIndexable(InAsset))
	{
		FAssetData InAssetData(InAsset);

		bool bWasIndexed = false;
		FString IndexedJson;
		{
			FSearchSerializer Serializer(InAssetData, &IndexedJson);

			UClass* IndexableClass = InAsset->GetClass();
			while (IndexableClass)
			{
				if (IAssetIndexer* Indexer = Indexers.FindRef(IndexableClass->GetFName()))
				{
					bWasIndexed = true;
					Serializer.BeginIndexer(Indexer);
					Indexer->IndexAsset(InAsset, Serializer);
					Serializer.EndIndexer();
				}

				IndexableClass = IndexableClass->GetSuperClass();
			}
		}

		if (bWasIndexed)
		{
			// Hash the content so that we can store it in the DDC.
			FSHAHash IndexedJsonHash;
			FSHA1::HashBuffer(*IndexedJson, IndexedJson.Len() * sizeof(FString::ElementType), IndexedJsonHash.Hash);

			const FString DerivedDataKey = bLegacyIndexing ? GetDerivedDataKey(InAssetData) : GetDerivedDataKey(IndexedJsonHash);

			FTCHARToUTF8 IndexedJsonUTF8(*IndexedJson);
			TArrayView<const uint8> IndexedJsonUTF8View((const uint8*)IndexedJsonUTF8.Get(), IndexedJsonUTF8.Length() * sizeof(UTF8CHAR));
			GetDerivedDataCacheRef().Put(*DerivedDataKey, IndexedJsonUTF8View, InAssetData.ObjectPath.ToString(), bLegacyIndexing);

			AddToContentTagCache(InAssetData, IndexedJson, DerivedDataKey);

			AddOrUpdateAsset(InAssetData, IndexedJson, DerivedDataKey);
		}
	}
}

void FAssetSearchManager::LoadDDCContentIntoDatabase(const FAssetData& InAsset, const TArray<uint8>& Content, const FString& DerivedDataKey)
{
	FUTF8ToTCHAR WByteBuffer((const ANSICHAR*)Content.GetData(), Content.Num());
	FString IndexedJson(WByteBuffer.Length(), WByteBuffer.Get());

	AddOrUpdateAsset(InAsset, IndexedJson, DerivedDataKey);
}

void FAssetSearchManager::AddOrUpdateAsset(const FAssetData& InAssetData, const FString& IndexedJson, const FString& DerivedDataKey)
{
	check(IsInGameThread());

	PendingDatabaseUpdates++;
	UpdateOperations.Enqueue([this, InAssetData, IndexedJson, DerivedDataKey]() {
		FScopeLock ScopedLock(&SearchDatabaseCS);
		SearchDatabase.AddOrUpdateAsset(InAssetData, IndexedJson, DerivedDataKey);
		PendingDatabaseUpdates--;
	});
}

bool FAssetSearchManager::Tick_GameThread(float DeltaTime)
{
	check(IsInGameThread());

	int32 ScanLimit = GameThread_AssetScanLimit;
	while (ProcessAssetQueue.Num() > 0 && ScanLimit > 0 && PendingDownloads < PendingDownloadsMax)
	{
		FAssetData Asset = ProcessAssetQueue.Pop(false);
		if (TryLoadIndexForAsset(Asset))
		{
			ScanLimit -= 10;
		}

		ScanLimit--;
	}

	int32 DownloadProcessLimit = GameThread_DownloadProcessLimit;
	while (!ProcessDDCQueue.IsEmpty() && DownloadProcessLimit > 0)
	{
		const FAssetDDCRequest* PendingRequest = ProcessDDCQueue.Peek();
		if (GetDerivedDataCacheRef().PollAsynchronousCompletion(PendingRequest->DDCHandle))
		{
			bool bDataWasBuilt;

			TArray<uint8> OutContent;
			bool bGetSuccessful = GetDerivedDataCacheRef().GetAsynchronousResults(PendingRequest->DDCHandle, OutContent, &bDataWasBuilt);
			if (bGetSuccessful)
			{
				LoadDDCContentIntoDatabase(PendingRequest->AssetData, OutContent, PendingRequest->DDCKey_IndexDataHash);
			}

			ProcessDDCQueue.Pop();
			PendingDownloads--;
			DownloadProcessLimit--;
			continue;
		}
		break;
	}

	if ((FPlatformTime::Seconds() - LastRecordCountUpdateSeconds) > 30)
	{
		LastRecordCountUpdateSeconds = FPlatformTime::Seconds();

		ImmediateOperations.Enqueue([this]() {
			FScopeLock ScopedLock(&SearchDatabaseCS);
			TotalSearchRecords = SearchDatabase.GetTotalSearchRecords();
		});
	}

	return true;
}

uint32 FAssetSearchManager::Run()
{
	Tick_DatabaseOperationThread();
	return 0;
}

void FAssetSearchManager::Tick_DatabaseOperationThread()
{
	while (RunThread)
	{
		TFunction<void()> Operation;
		if (ImmediateOperations.Dequeue(Operation) || FeedOperations.Dequeue(Operation) || UpdateOperations.Dequeue(Operation))
		{
			Operation();
		}
		else
		{
			FPlatformProcess::Sleep(0.1);
		}
	}
}

void FAssetSearchManager::Search(const FSearchQuery& Query, TFunction<void(TArray<FSearchRecord>&&)> InCallback)
{
	check(IsInGameThread());

	ImmediateOperations.Enqueue([this, Query, InCallback]() {

		TArray<FSearchRecord> Results;

		{
			FScopeLock ScopedLock(&SearchDatabaseCS);
			SearchDatabase.EnumerateSearchResults(Query, [&Results](FSearchRecord&& InResult) {
				Results.Add(MoveTemp(InResult));
				return true;
			});
		}

		AsyncTask(ENamedThreads::GameThread, [ResultsFwd = MoveTemp(Results), InCallback]() mutable {
			InCallback(MoveTemp(ResultsFwd));
		});
	});
}