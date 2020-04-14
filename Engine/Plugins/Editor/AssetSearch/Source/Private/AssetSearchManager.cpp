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
#include "StudioAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "Misc/FeedbackContext.h"
#include "WidgetBlueprint.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "Indexers/DialogueWaveIndexer.h"
#include "Sound/DialogueWave.h"
#include "Indexers/LevelIndexer.h"
#include "Settings/SearchProjectSettings.h"
#include "Settings/SearchUserSettings.h"
#include "Indexers/SoundCueIndexer.h"
#include "Sound/SoundCue.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/StringBuilder.h"
#include "Engine/World.h"
#include "Editor.h"
#include "PackageTools.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectKey.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "FileHelpers.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "FAssetSearchManager"

static bool bForceEnableSearch = false;
FAutoConsoleVariableRef CVarDisableUniversalSearch(
	TEXT("Search.ForceEnable"),
	bForceEnableSearch,
	TEXT("Enable universal search")
);

static bool bTryIndexAssetsOnLoad = false;
FAutoConsoleVariableRef CVarTryIndexAssetsOnLoad(
	TEXT("Search.TryIndexAssetsOnLoad"),
	bTryIndexAssetsOnLoad,
	TEXT("Tries to index assets on load.")
);

class FUnloadPackageScope
{
public:
	FUnloadPackageScope()
	{
		FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FUnloadPackageScope::OnAssetLoaded);
	}

	~FUnloadPackageScope()
	{
		FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
		TryUnload(true);
	}

	int32 TryUnload(bool bResetTrackedObjects)
	{
		TArray<TWeakObjectPtr<UObject>> PackageObjectPtrs;

		for (const FObjectKey& LoadedObjectKey : ObjectsLoaded)
		{
			if (UObject* LoadedObject = LoadedObjectKey.ResolveObjectPtr())
			{
				UPackage* Package = LoadedObject->GetOutermost();

				TArray<UObject*> PackageObjects;
				GetObjectsWithOuter(Package, PackageObjects, false);

				for (UObject* PackageObject : PackageObjects)
				{
					PackageObject->ClearFlags(RF_Standalone);
					PackageObjectPtrs.Add(PackageObject);
				}
			}
		}

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		
		int32 NumRemoved = 0;
		for (int32 LoadedAssetIndex = 0; LoadedAssetIndex < PackageObjectPtrs.Num(); LoadedAssetIndex++)
		{
			TWeakObjectPtr<UObject> PackageObjectPtr = PackageObjectPtrs[LoadedAssetIndex];

			if (UObject* LoadedObject = PackageObjectPtr.Get())
			{
				//FReferencerInformationList ReferencesIncludingUndo;
				//bool bReferencedInMemoryOrUndoStack = IsReferenced(LoadedObject, GARBAGE_COLLECTION_KEEPFLAGS, EInternalObjectFlags::GarbageCollectionKeepFlags, true, &ReferencesIncludingUndo);

				LoadedObject->SetFlags(RF_Standalone);
			}
			else
			{
				NumRemoved++;
			}
		}

		if (bResetTrackedObjects)
		{
			ObjectsLoaded.Reset();
		}
		else
		{
			for (int32 ObjectIndex = 0; ObjectIndex < ObjectsLoaded.Num(); ObjectIndex++)
			{
				if (!ObjectsLoaded[ObjectIndex].ResolveObjectPtr())
				{
					ObjectsLoaded.RemoveAt(ObjectIndex);
					ObjectIndex--;
				}
			}
		}

		return NumRemoved;
	}

	int32 GetObjectsLoaded() const
	{
		return ObjectsLoaded.Num();
	}

private:
	void OnAssetLoaded(UObject* InObject)
	{
		ObjectsLoaded.Add(FObjectKey(InObject));
	}

private:
	TArray<FObjectKey> ObjectsLoaded;
	TArray<UClass*> ClassFilters;
};


FAssetSearchManager::FAssetSearchManager()
{
	PendingDatabaseUpdates = 0;
	IsAssetUpToDateCount = 0;
	ActiveDownloads = 0;
	DownloadQueueCount = 0;
	TotalSearchRecords = 0;
	LastRecordCountUpdateSeconds = 0;

	RunThread = false;
}

FAssetSearchManager::~FAssetSearchManager()
{
	RunThread = false;

	if (DatabaseThread)
	{
		DatabaseThread->WaitForCompletion();
	}

	StopScanningAssets();

	UPackage::PackageSavedEvent.RemoveAll(this);
	FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
	UObject::FAssetRegistryTag::OnGetExtraObjectTags.RemoveAll(this);

	FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
}

void FAssetSearchManager::Start()
{
	RegisterAssetIndexer(UDataAsset::StaticClass(), MakeUnique<FDataAssetIndexer>());
	RegisterAssetIndexer(UDataTable::StaticClass(), MakeUnique<FDataTableIndexer>());
	RegisterAssetIndexer(UBlueprint::StaticClass(), MakeUnique<FBlueprintIndexer>());
	RegisterAssetIndexer(UWidgetBlueprint::StaticClass(), MakeUnique<FWidgetBlueprintIndexer>());
	RegisterAssetIndexer(UDialogueWave::StaticClass(), MakeUnique<FDialogueWaveIndexer>());
	RegisterAssetIndexer(UWorld::StaticClass(), MakeUnique<FLevelIndexer>());
	RegisterAssetIndexer(USoundCue::StaticClass(), MakeUnique<FSoundCueIndexer>());

	UPackage::PackageSavedEvent.AddRaw(this, &FAssetSearchManager::HandlePackageSaved);
	FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FAssetSearchManager::OnAssetLoaded);

	TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAssetSearchManager::Tick_GameThread), 0);

	RunThread = true;
	DatabaseThread = FRunnableThread::Create(this, TEXT("UniversalSearch"), 0, TPri_BelowNormal);
}

void FAssetSearchManager::UpdateScanningAssets()
{
	bool bTargetState = GetDefault<USearchUserSettings>()->bEnableSearch;

	if (GIsBuildMachine || FApp::IsUnattended())
	{
		bTargetState = false;
	}

	if (bForceEnableSearch)
	{
		bTargetState = true;
	}

	if (bTargetState != bStarted)
	{
		bStarted = bTargetState;

		if (bTargetState)
		{
			StartScanningAssets();
		}
		else
		{
			StopScanningAssets();
		}
	}
}

void FAssetSearchManager::StartScanningAssets()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.OnAssetAdded().AddRaw(this, &FAssetSearchManager::OnAssetAdded);
	AssetRegistry.OnAssetRemoved().AddRaw(this, &FAssetSearchManager::OnAssetRemoved);
	AssetRegistry.OnFilesLoaded().AddRaw(this, &FAssetSearchManager::OnAssetScanFinished);
	
	TArray<FAssetData> TempAssetData;
	AssetRegistry.GetAllAssets(TempAssetData, true);

	for (const FAssetData& Data : TempAssetData)
	{
		OnAssetAdded(Data);
	}
}

void FAssetSearchManager::StopScanningAssets()
{
	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		AssetRegistryModule->Get().OnAssetAdded().RemoveAll(this);
		AssetRegistryModule->Get().OnAssetRemoved().AddRaw(this, &FAssetSearchManager::OnAssetRemoved);
		AssetRegistryModule->Get().OnFilesLoaded().AddRaw(this, &FAssetSearchManager::OnAssetScanFinished);
	}

	ProcessAssetQueue.Reset();
	FailedDDCRequests.Reset();
}

void FAssetSearchManager::TryConnectToDatabase()
{
	if (!bDatabaseOpen)
	{
		if ((FPlatformTime::Seconds() - LastConnectionAttempt) > 30)
		{
			LastConnectionAttempt = FPlatformTime::Seconds();

			const FString SessionPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Search")));
			
			if (!FileInfoDatabase.Open(SessionPath))
			{
				return;
			}

			if (!SearchDatabase.Open(SessionPath))
			{
				FileInfoDatabase.Close();
				return;
			}

			bDatabaseOpen = true;
		}
	}
}

FSearchStats FAssetSearchManager::GetStats() const
{
	FSearchStats Stats;
	Stats.Scanning = ProcessAssetQueue.Num();
	Stats.Processing = IsAssetUpToDateCount + DownloadQueueCount + ActiveDownloads;
	Stats.Updating = PendingDatabaseUpdates;
	Stats.TotalRecords = TotalSearchRecords;
	Stats.AssetsMissingIndex = FailedDDCRequests.Num();
	return Stats;
}

void FAssetSearchManager::RegisterAssetIndexer(const UClass* AssetClass, TUniquePtr<IAssetIndexer>&& Indexer)
{
	check(IsInGameThread());

	Indexers.Add(AssetClass->GetFName(), MoveTemp(Indexer));
}

void FAssetSearchManager::OnAssetAdded(const FAssetData& InAssetData)
{
	check(IsInGameThread());

	static const FString DeveloperPathWithSlash = FPackageName::FilenameToLongPackageName(FPaths::GameDevelopersDir());
	static const FString UsersDeveloperPathWithSlash = FPackageName::FilenameToLongPackageName(FPaths::GameUserDeveloperDir());
	
	// Don't process stuff in the other developer folders.
	FString PackageName = InAssetData.PackageName.ToString();
	if (PackageName.StartsWith(DeveloperPathWithSlash))
	{
		if (!PackageName.StartsWith(UsersDeveloperPathWithSlash))
		{
			return;
		}
	}

	// 
	const USearchProjectSettings* ProjectSettings = GetDefault<USearchProjectSettings>();
	for (const FDirectoryPath& IgnoredPath : ProjectSettings->IgnoredPaths)
	{
		if (PackageName.StartsWith(IgnoredPath.Path))
		{
			return;
		}
	}

	// 
	const USearchUserSettings* UserSettings = GetDefault<USearchUserSettings>();
	for (const FDirectoryPath& IgnoredPath : UserSettings->IgnoredPaths)
	{
		if (PackageName.StartsWith(IgnoredPath.Path))
		{
			return;
		}
	}

	// Don't index redirectors, just act like they don't exist.
	if (InAssetData.IsRedirector())
	{
		return;
	}

	FAssetOperation Operation;
	Operation.Asset = InAssetData;
	ProcessAssetQueue.Add(Operation);
}

void FAssetSearchManager::OnAssetRemoved(const FAssetData& InAssetData)
{
	check(IsInGameThread());

	FAssetOperation Operation;
	Operation.Asset = InAssetData;
	Operation.bRemoval = true;
	ProcessAssetQueue.Add(Operation);
}

void FAssetSearchManager::OnAssetScanFinished()
{
	check(IsInGameThread());

	TArray<FAssetData> AllAssets;
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.GetAllAssets(AllAssets, false);
	
	PendingDatabaseUpdates++;
	UpdateOperations.Enqueue([this, AssetsAvailable = MoveTemp(AllAssets)]() mutable {
		FScopeLock ScopedLock(&SearchDatabaseCS);
		SearchDatabase.RemoveAssetsNotInThisSet(AssetsAvailable);
		PendingDatabaseUpdates--;
	});
}

void FAssetSearchManager::HandlePackageSaved(const FString& PackageFilename, UObject* Outer)
{
	check(IsInGameThread());

	// Ignore package operations fired by the cooker (cook on the fly).
	if (GIsCookerLoadingPackage)
	{
		return;
	}

	UPackage* Package = CastChecked<UPackage>(Outer);

	if (GIsEditor && !IsRunningCommandlet())
	{
		TArray<UObject*> Objects;
		const bool bIncludeNestedObjects = false;
		GetObjectsWithOuter(Package, Objects, bIncludeNestedObjects);
		for (UObject* Entry : Objects)
		{
			RequestIndexAsset(Entry);
		}
	}
}

void FAssetSearchManager::OnAssetLoaded(UObject* InObject)
{
	check(IsInGameThread());

	if (bTryIndexAssetsOnLoad)
	{
		RequestIndexAsset(InObject);
	}
}

bool FAssetSearchManager::RequestIndexAsset(UObject* InAsset)
{
	check(IsInGameThread());

	if (GEditor == nullptr || GEditor->IsAutosaving())
	{
		return false;
	}

	if (IsAssetIndexable(InAsset))
	{
		TWeakObjectPtr<UObject> AssetWeakPtr = InAsset;
		FAssetData AssetData(InAsset);

		return AsyncGetDerivedDataKey(AssetData, [this, AssetData, AssetWeakPtr](FString InDDCKey) {
			UpdateOperations.Enqueue([this, AssetData, AssetWeakPtr, InDDCKey]() {
				FScopeLock ScopedLock(&SearchDatabaseCS);
				if (!SearchDatabase.IsAssetUpToDate(AssetData, InDDCKey))
				{
					AsyncMainThreadTask([this, AssetWeakPtr]() {
						StoreIndexForAsset(AssetWeakPtr.Get());
					});
				}
			});
		});
	}

	return false;
}

bool FAssetSearchManager::IsAssetIndexable(UObject* InAsset)
{
	if (InAsset && InAsset->IsAsset())
	{
		// If it's not a permanent package, and one we just loaded for diffing, don't index it.
		UPackage* Package = InAsset->GetOutermost();
		if (Package->HasAnyPackageFlags(/*LOAD_ForDiff | */LOAD_PackageForPIE | LOAD_ForFileDiff))
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

bool FAssetSearchManager::TryLoadIndexForAsset(const FAssetData& InAssetData)
{
	const bool bSuccess = AsyncGetDerivedDataKey(InAssetData, [this, InAssetData](FString InDDCKey) {
		FeedOperations.Enqueue([this, InAssetData, InDDCKey]() {
			FScopeLock ScopedLock(&SearchDatabaseCS);
			if (!SearchDatabase.IsAssetUpToDate(InAssetData, InDDCKey))
			{
				AsyncRequestDownlaod(InAssetData, InDDCKey);
			}

			IsAssetUpToDateCount--;
		});
	});

	if (bSuccess)
	{
		IsAssetUpToDateCount++;
	}

	return bSuccess;
}

void FAssetSearchManager::AsyncRequestDownlaod(const FAssetData& InAssetData, const FString& InDDCKey)
{
	DownloadQueueCount++;

	FAssetDDCRequest DDCRequest;
	DDCRequest.AssetData = InAssetData;
	DDCRequest.DDCKey = InDDCKey;
	DownloadQueue.Enqueue(DDCRequest);
}

bool FAssetSearchManager::AsyncGetDerivedDataKey(const FAssetData& InAssetData, TFunction<void(FString)> DDCKeyCallback)
{
	check(IsInGameThread());

	FString IndexersNamesAndVersions = GetIndexerVersion(InAssetData.GetClass());

	// If the indexer names and versions is empty, then we know it's not possible to index this type of thing.
	if (IndexersNamesAndVersions.IsEmpty())
	{
		return false;
	}

	UpdateOperations.Enqueue([this, InAssetData, IndexersNamesAndVersions, DDCKeyCallback]() {
		FAssetFileInfo FileInfo;

		{
			FScopeLock ScopedLock(&FileInfoDatabaseCS);
			FileInfoDatabase.AddOrUpdateFileInfo(InAssetData, FileInfo);
		}

		if (FileInfo.Hash.IsValid())
		{
			// The universal key for content is:
			// AssetSearch_V{SerializerVersion}_{IndexersNamesAndVersions}_{ObjectPathHash}_{FileOnDiskHash}

			const FString ObjectPathString = InAssetData.ObjectPath.ToString();

			FSHAHash ObjectPathHash;
			FSHA1::HashBuffer(*ObjectPathString, ObjectPathString.Len() * sizeof(FString::ElementType), ObjectPathHash.Hash);

			TStringBuilder<512> DDCKey;
			DDCKey.Append(TEXT("AssetSearch_V"));
			DDCKey.Append(LexToString(FSearchSerializer::GetVersion()));
			DDCKey.Append(TEXT("_"));
			DDCKey.Append(IndexersNamesAndVersions);
			DDCKey.Append(TEXT("_"));
			DDCKey.Append(ObjectPathHash.ToString());
			DDCKey.Append(TEXT("_"));
			DDCKey.Append(LexToString(FileInfo.Hash));

			const FString DDCKeyString = DDCKey.ToString();

			DDCKeyCallback(DDCKeyString);
		}
	});

	return true;
}

bool FAssetSearchManager::HasIndexerForClass(const UClass* InAssetClass) const
{
	const UClass* IndexableClass = InAssetClass;
	while (IndexableClass)
	{
		if (Indexers.Contains(IndexableClass->GetFName()))
		{
			return true;
		}

		IndexableClass = IndexableClass->GetSuperClass();
	}

	return false;
}

FString FAssetSearchManager::GetIndexerVersion(const UClass* InAssetClass) const
{
	TStringBuilder<256> VersionString;

	TArray<UClass*> NestedIndexedTypes;

	const UClass* IndexableClass = InAssetClass;
	while (IndexableClass)
	{
		if (const TUniquePtr<IAssetIndexer>* IndexerPtr = Indexers.Find(IndexableClass->GetFName()))
		{
			IAssetIndexer* Indexer = IndexerPtr->Get();
			VersionString.Append(Indexer->GetName());
			VersionString.Append(TEXT("_"));
			VersionString.Append(LexToString(Indexer->GetVersion()));

			Indexer->GetNestedAssetTypes(NestedIndexedTypes);
		}

		IndexableClass = IndexableClass->GetSuperClass();
	}

	for (UClass* NestedIndexedType : NestedIndexedTypes)
	{
		VersionString.Append(GetIndexerVersion(NestedIndexedType));
	}

	return VersionString.ToString();
}

void FAssetSearchManager::StoreIndexForAsset(UObject* InAsset)
{
	check(IsInGameThread());

	if (IsAssetIndexable(InAsset) && HasIndexerForClass(InAsset->GetClass()))
	{
		FAssetData InAssetData(InAsset);

		FString IndexedJson;
		bool bWasIndexed = false;
		{
			FSearchSerializer Serializer(InAssetData, &IndexedJson);
			bWasIndexed = Serializer.IndexAsset(InAsset, Indexers);
		}

		if (bWasIndexed && !IndexedJson.IsEmpty())
		{
			AsyncGetDerivedDataKey(InAssetData, [this, InAssetData, IndexedJson](FString InDDCKey) {
				AsyncMainThreadTask([this, InAssetData, IndexedJson, InDDCKey]() {
					check(IsInGameThread());

					FTCHARToUTF8 IndexedJsonUTF8(*IndexedJson);
					TArrayView<const uint8> IndexedJsonUTF8View((const uint8*)IndexedJsonUTF8.Get(), IndexedJsonUTF8.Length() * sizeof(UTF8CHAR));
					GetDerivedDataCacheRef().Put(*InDDCKey, IndexedJsonUTF8View, InAssetData.ObjectPath.ToString(), false);

					AddOrUpdateAsset(InAssetData, IndexedJson, InDDCKey);
				});
			});
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

	UpdateScanningAssets();

	TryConnectToDatabase();

	ProcessGameThreadTasks();

	const USearchUserSettings* UserSettings = GetDefault<USearchUserSettings>();
	const FSearchPerformance& PerformanceLimits = bForceEnableSearch ? UserSettings->DefaultOptions : UserSettings->GetPerformanceOptions();

	int32 ScanLimit = PerformanceLimits.AssetScanRate;
	while (ProcessAssetQueue.Num() > 0 && ScanLimit > 0)
	{
		FAssetOperation Operation = ProcessAssetQueue.Pop(false);
		FAssetData Asset = Operation.Asset;

		if (Operation.bRemoval)
		{
			PendingDatabaseUpdates++;
			UpdateOperations.Enqueue([this, Asset]() {
				FScopeLock ScopedLock(&SearchDatabaseCS);
				SearchDatabase.RemoveAsset(Asset);
				PendingDatabaseUpdates--;
			});
		}
		else
		{
			TryLoadIndexForAsset(Asset);
		}

		ScanLimit--;
	}

	while (!DownloadQueue.IsEmpty() && ActiveDownloads < PerformanceLimits.ParallelDownloads)
	{
		FAssetDDCRequest DDCRequest;
		bool bSuccess = DownloadQueue.Dequeue(DDCRequest);
		check(bSuccess);

		DownloadQueueCount--;
		ActiveDownloads++;

		DDCRequest.DDCHandle = GetDerivedDataCacheRef().GetAsynchronous(*DDCRequest.DDCKey, DDCRequest.AssetData.ObjectPath.ToString());
		ProcessDDCQueue.Enqueue(DDCRequest);
	}

	int32 MaxQueueProcesses = 1000;
	int32 DownloadProcessLimit = PerformanceLimits.DownloadProcessRate;
	while (!ProcessDDCQueue.IsEmpty() && DownloadProcessLimit > 0 && MaxQueueProcesses > 0)
	{
		const FAssetDDCRequest* PendingRequest = ProcessDDCQueue.Peek();
		if (GetDerivedDataCacheRef().PollAsynchronousCompletion(PendingRequest->DDCHandle))
		{
			bool bDataWasBuilt;

			TArray<uint8> OutContent;
			bool bGetSuccessful = GetDerivedDataCacheRef().GetAsynchronousResults(PendingRequest->DDCHandle, OutContent, &bDataWasBuilt);
			if (bGetSuccessful)
			{
				LoadDDCContentIntoDatabase(PendingRequest->AssetData, OutContent, PendingRequest->DDCKey);
				DownloadProcessLimit--;
			}
			else if (UserSettings->bShowMissingAssets)
			{
				FailedDDCRequests.Add(*PendingRequest);
			}

			ProcessDDCQueue.Pop();
			ActiveDownloads--;
			MaxQueueProcesses--;
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
		if (!bDatabaseOpen)
		{
			FPlatformProcess::Sleep(1);
			continue;
		}

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

void FAssetSearchManager::ForceIndexOnAssetsMissingIndex()
{
	check(IsInGameThread());

	EAppReturnType::Type IncludeMaps = FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("IncludeMaps", "Do you want to open and index map files, this can take a long time?"));

	FScopedSlowTask IndexingTask(FailedDDCRequests.Num(), LOCTEXT("ForceIndexOnAssetsMissingIndex", "Indexing Assets"));
	IndexingTask.MakeDialog(true);

	int32 RemovedCount = 0;

	TArray<FAssetData> RedirectorsWithBrokenMetadata;

	FUnloadPackageScope UnloadScope;
	for (const FAssetDDCRequest& Request : FailedDDCRequests)
	{
		if (IndexingTask.ShouldCancel())
		{
			break;
		}

		if (IncludeMaps != EAppReturnType::Yes)
		{
			if (Request.AssetData.GetClass() == UWorld::StaticClass())
			{
				RemovedCount++;
				continue;
			}
		}

		ProcessGameThreadTasks();

		IndexingTask.EnterProgressFrame(1, FText::Format(LOCTEXT("ForceIndexOnAssetsMissingIndexFormat", "Indexing Asset ({0} of {1})"), RemovedCount + 1, FailedDDCRequests.Num()));
		if (UObject* AssetToIndex = Request.AssetData.GetAsset())
		{
			// This object's metadata incorrectly labled it as something other than a redirector.  We need to resave it
			// to stop it from appearing as something it's not.
			if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetToIndex))
			{
				RedirectorsWithBrokenMetadata.Add(Request.AssetData);
				RemovedCount++;
				continue;
			}

			if (!bTryIndexAssetsOnLoad)
			{
				StoreIndexForAsset(AssetToIndex);
			}
		}

		if (UnloadScope.GetObjectsLoaded() > 2000)
		{
			UnloadScope.TryUnload(true);
		}

		RemovedCount++;
	}

	if (RedirectorsWithBrokenMetadata.Num() > 0)
	{
		EAppReturnType::Type ResaveRedirectors = FMessageDialog::Open(EAppMsgType::YesNo,
			LOCTEXT("ResaveRedirectors", "We found some redirectors that didn't have the correct asset metadata identifying them as redirectors.  Would you like to resave them, so that they stop appearing as missing asset indexes?"));

		if (ResaveRedirectors == EAppReturnType::Yes)
		{
			TArray<UPackage*> PackagesToSave;
			for (const FAssetData& BrokenAsset : RedirectorsWithBrokenMetadata)
			{
				if (UObject* Redirector = BrokenAsset.GetAsset())
				{
					PackagesToSave.Add(Redirector->GetOutermost());
				}
			}

			FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty*/false, /*bPromptToSave*/false);
		}
	}

	FailedDDCRequests.RemoveAtSwap(0, RemovedCount);
}

void FAssetSearchManager::Search(const FSearchQuery& Query, TFunction<void(TArray<FSearchRecord>&&)> InCallback)
{
	check(IsInGameThread());

	FStudioAnalytics::RecordEvent(TEXT("AssetSearch"), {
		FAnalyticsEventAttribute(TEXT("QueryString"), Query.Query)
	});

	ImmediateOperations.Enqueue([this, Query, InCallback]() {

		TArray<FSearchRecord> Results;

		{
			FScopeLock ScopedLock(&SearchDatabaseCS);
			SearchDatabase.EnumerateSearchResults(Query, [&Results](FSearchRecord&& InResult) {
				Results.Add(MoveTemp(InResult));
				return true;
			});
		}

		AsyncMainThreadTask([ResultsFwd = MoveTemp(Results), InCallback]() mutable {
			InCallback(MoveTemp(ResultsFwd));
		});
	});
}

void FAssetSearchManager::AsyncMainThreadTask(TFunction<void()> Task)
{
	GT_Tasks.Enqueue(Task);
}

void FAssetSearchManager::ProcessGameThreadTasks()
{
	if (!GT_Tasks.IsEmpty())
	{
		if (GIsSavingPackage)
		{
			// If we're saving packages just give up, the call in Tick_GameThread will do this later.
			return;
		}

		int MaxGameThreadTasksPerTick = 1000;

		TFunction<void()> Operation;
		while (GT_Tasks.Dequeue(Operation) && MaxGameThreadTasksPerTick > 0)
		{
			Operation();
			MaxGameThreadTasksPerTick--;
		}
	}
}

#undef LOCTEXT_NAMESPACE
