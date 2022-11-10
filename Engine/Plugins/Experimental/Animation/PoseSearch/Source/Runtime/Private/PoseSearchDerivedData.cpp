// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchDerivedData.h"

#if WITH_EDITOR
#include "Animation/BlendSpace.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "Misc/CoreDelegates.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/BulkDataRegistry.h"
#include "UObject/NoExportTypes.h"

namespace UE::PoseSearch
{
	static const UE::DerivedData::FValueId Id(UE::DerivedData::FValueId::FromName("Data"));
	static const UE::DerivedData::FCacheBucket Bucket("PoseSearchDatabase");

#if ENABLE_COOK_STATS
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
		{
			UsageStats.LogStats(AddStat, TEXT("MotionMatching.Usage"), TEXT(""));
		});
#endif

	struct FPoseSearchDatabaseAsyncCacheTask
	{
		FPoseSearchDatabaseAsyncCacheTask(UPoseSearchDatabase& InDatabase);
		~FPoseSearchDatabaseAsyncCacheTask();

		void Cancel();
		void Wait();
		bool Poll() const;

		UPoseSearchDatabase& GetDatabase() { return Database; }
		const UPoseSearchDatabase& GetDatabase() const { return Database; }
		static FIoHash CreateKey(UPoseSearchDatabase& Database);

	private:
		FPoseSearchDatabaseAsyncCacheTask(const FPoseSearchDatabaseAsyncCacheTask& Other) = delete;
		FPoseSearchDatabaseAsyncCacheTask(FPoseSearchDatabaseAsyncCacheTask&& Other) = delete;
		FPoseSearchDatabaseAsyncCacheTask& operator=(const FPoseSearchDatabaseAsyncCacheTask& Other) = delete;
		FPoseSearchDatabaseAsyncCacheTask& operator=(FPoseSearchDatabaseAsyncCacheTask&& Other) = delete;

		void OnGetComplete(UE::DerivedData::FCacheGetResponse&& Response);

		UPoseSearchDatabase& Database;
		FPoseSearchIndex SearchIndex;
		UE::DerivedData::FRequestOwner Owner;
	};

	class FPoseSearchDatabaseAsyncCacheTasks : public TArray<TUniquePtr<FPoseSearchDatabaseAsyncCacheTask>> {};

	FPoseSearchDatabaseAsyncCacheTask::FPoseSearchDatabaseAsyncCacheTask(UPoseSearchDatabase& InDatabase)
		: Database(InDatabase)
		, SearchIndex()
		, Owner(UE::DerivedData::EPriority::Normal)
	{
		using namespace UE::DerivedData;
		const FIoHash PendingDerivedDataKey = CreateKey(Database);

		Database.NotifyDerivedDataRebuild(UPoseSearchDatabase::EDerivedDataBuildState::Prestarted);

		UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BeginCache"), *LexToString(PendingDerivedDataKey), *Database.GetName());

		TArray<FCacheGetRequest> CacheRequests;
		const FCacheKey CacheKey{ Bucket, PendingDerivedDataKey };
		CacheRequests.Add({ { Database.GetPathName() }, CacheKey, ECachePolicy::Default });
		GetCache().Get(CacheRequests, Owner, [this](FCacheGetResponse&& Response)
			{
				OnGetComplete(MoveTemp(Response));
			});
	}

	FPoseSearchDatabaseAsyncCacheTask::~FPoseSearchDatabaseAsyncCacheTask()
	{
		check(IsInGameThread());
		check(Poll());
		if (!Owner.IsCanceled())
		{
			// @todo: implement FPoseSearchIndex move ctor and assignment operator and use a MoveTemp(SearchIndex) here
			Database.PoseSearchIndex = SearchIndex;

			Database.NotifyDerivedDataRebuild(UPoseSearchDatabase::EDerivedDataBuildState::Ended);
		}
		else
		{
			Database.NotifyDerivedDataRebuild(UPoseSearchDatabase::EDerivedDataBuildState::Cancelled);
		}
	}

	void FPoseSearchDatabaseAsyncCacheTask::Cancel()
	{
		Owner.Cancel();
	}

	void FPoseSearchDatabaseAsyncCacheTask::Wait()
	{
		Owner.Wait();
	}

	bool FPoseSearchDatabaseAsyncCacheTask::Poll() const
	{
		return Owner.Poll();
	}

	void FPoseSearchDatabaseAsyncCacheTask::OnGetComplete(UE::DerivedData::FCacheGetResponse&& Response)
	{
		using namespace UE::DerivedData;

		const FCacheKey Key = Response.Record.GetKey();
		if (Response.Status == EStatus::Ok)
		{
			COOK_STAT(auto Timer = UsageStats.TimeAsyncWait());

			// we found the cached data associated to the PendingDerivedDataKey: we'll deserialized into SearchIndex
			SearchIndex.Reset();
			FSharedBuffer RawData = Response.Record.GetValue(Id).GetData().Decompress();
			FMemoryReaderView Reader(RawData);
			Reader << SearchIndex;

			UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex From Cache"), *LexToString(Key.Hash), *Database.GetName());

			COOK_STAT(Timer.AddHit(RawData.GetSize()));
		}
		else if (Response.Status == EStatus::Error)
		{
			// we didn't find the cached data associated to the PendingDerivedDataKey: we'll BuildIndex to update SearchIndex and "Put" the data over the DDC
			Owner.LaunchTask(TEXT("PoseSearchDatabaseBuild"), [this, Key]
				{
					COOK_STAT(auto Timer = UsageStats.TimeSyncWork());

					int32 BytesProcessed = 0;
					if (BuildIndex(Database, SearchIndex, Owner))
					{
						UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Succeeded"), *LexToString(Key.Hash), *Database.GetName());

						TArray<uint8> RawBytes;
						FMemoryWriter Writer(RawBytes);
						Writer << SearchIndex;
						FSharedBuffer RawData = MakeSharedBufferFromArray(MoveTemp(RawBytes));
						BytesProcessed = RawData.GetSize();

						FCacheRecordBuilder Builder(Key);
						Builder.AddValue(Id, RawData);
						GetCache().Put({ { { Database.GetPathName() }, Builder.Build() } }, Owner, [this, Key](FCachePutResponse&& Response)
							{
								if (Response.Status == EStatus::Error)
								{
									UE_LOG(LogPoseSearch, Log, TEXT("%s - %s Failed to store DDC"), *LexToString(Key.Hash), *Database.GetName());
								}
							});
					}
					else
					{
						if (Owner.IsCanceled())
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(Key.Hash), *Database.GetName());
						}
						else
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Failed"), *LexToString(Key.Hash), *Database.GetName());
						}
						SearchIndex.Reset();
					}

					COOK_STAT(Timer.AddMiss(BytesProcessed));
				});
		}
		else if(Response.Status == EStatus::Canceled)
		{
			SearchIndex.Reset();
			UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(Key.Hash), *Database.GetName());
		}
	}

	FIoHash FPoseSearchDatabaseAsyncCacheTask::CreateKey(UPoseSearchDatabase& Database)
	{
		using UE::PoseSearch::FDerivedDataKeyBuilder;

		FDerivedDataKeyBuilder KeyBuilder;
		FGuid VersionGuid = FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().POSESEARCHDB_DERIVEDDATA_VER);
		KeyBuilder << VersionGuid;
		Database.BuildDerivedDataKey(KeyBuilder);

		// Stores a BLAKE3-160 hash, taken from the first 20 bytes of a BLAKE3-256 hash
		FIoHash IoHash(KeyBuilder.Finalize());
		return IoHash;
	}

	//////////////////////////////////////////////////////////////////////////
	// FAsyncPoseSearchDatabasesManagement
	FAsyncPoseSearchDatabasesManagement& FAsyncPoseSearchDatabasesManagement::Get()
	{
		static FAsyncPoseSearchDatabasesManagement SingletonInstance;
		return SingletonInstance;
	}

	FAsyncPoseSearchDatabasesManagement::FAsyncPoseSearchDatabasesManagement()
		: Tasks(*(new FPoseSearchDatabaseAsyncCacheTasks()))
	{
		OnObjectPreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::OnObjectPreSave); 
		OnPreObjectPropertyChangedHandle = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::OnPreObjectPropertyChanged);
		OnObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::OnObjectPropertyChanged);

		FCoreDelegates::OnPreExit.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::Shutdown);
	}

	FAsyncPoseSearchDatabasesManagement::~FAsyncPoseSearchDatabasesManagement()
	{
		FCoreDelegates::OnPreExit.RemoveAll(this);
		Shutdown();

		delete &Tasks;
	}

	void GetPoseSearchDatabaseAssetDataList(TArray<FAssetData>& OutPoseSearchDatabaseAssetDataList)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FARFilter Filter;
		Filter.bRecursiveClasses = true;
		Filter.ClassPaths.Add(UPoseSearchDatabase::StaticClass()->GetClassPathName());

		OutPoseSearchDatabaseAssetDataList.Reset();
		AssetRegistryModule.Get().GetAssets(Filter, OutPoseSearchDatabaseAssetDataList);
	}

	FPoseSearchDatabaseAsyncCacheTask& FAsyncPoseSearchDatabasesManagement::GetTask(int32 TaskIndex)
	{
		return *Tasks[TaskIndex].Get();
	}

	const FPoseSearchDatabaseAsyncCacheTask& FAsyncPoseSearchDatabasesManagement::GetTask(int32 TaskIndex) const
	{
		return *Tasks[TaskIndex].Get();
	}

	void FAsyncPoseSearchDatabasesManagement::RemoveTask(int32 TaskIndex)
	{
		Tasks.RemoveAtSwap(TaskIndex, 1, false);
	}

	void FAsyncPoseSearchDatabasesManagement::ForEachPoseSearchDatabase(bool bUseTasksDatabases, TFunctionRef<void(UPoseSearchDatabase&)> InFunction)
	{
		if (bUseTasksDatabases)
		{
			for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
			{
				UPoseSearchDatabase& PoseSearchDb = GetTask(TaskIndex).GetDatabase();
				InFunction(PoseSearchDb);
			}
		}
		else
		{
			TArray<FAssetData> PoseSearchDatabaseAssetDataList;
			GetPoseSearchDatabaseAssetDataList(PoseSearchDatabaseAssetDataList);
			for (const auto& PoseSearchDbAssetData : PoseSearchDatabaseAssetDataList)
			{
				if (UPoseSearchDatabase* PoseSearchDb = Cast<UPoseSearchDatabase>(PoseSearchDbAssetData.FastGetAsset(false)))
				{
					InFunction(*PoseSearchDb);
				}
			}
		}
	}

	void FAsyncPoseSearchDatabasesManagement::ExecuteIfObjectIsReferencedByDatabase(UObject* Object, bool bUseTasksDatabases, TFunctionRef<void(UPoseSearchDatabase&)> InFunction)
	{
		if (UAnimSequence* Sequence = Cast<UAnimSequence>(Object))
		{
			ForEachPoseSearchDatabase(bUseTasksDatabases, [Sequence, InFunction](UPoseSearchDatabase& PoseSearchDb)
				{
					const bool bSequenceFound = PoseSearchDb.Sequences.ContainsByPredicate([Sequence](FPoseSearchDatabaseSequence& DbSequence)
						{
							return Sequence == DbSequence.Sequence || Sequence == DbSequence.LeadInSequence || Sequence == DbSequence.FollowUpSequence;
						});

					if (bSequenceFound)
					{
						InFunction(PoseSearchDb);
					}
				});
		}
		else if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(Object))
		{
			ForEachPoseSearchDatabase(bUseTasksDatabases, [BlendSpace, InFunction](UPoseSearchDatabase& PoseSearchDb)
				{
					const bool bBlendSpaceFound = PoseSearchDb.BlendSpaces.ContainsByPredicate([BlendSpace](FPoseSearchDatabaseBlendSpace& DbBlendSpace)
						{
							return BlendSpace == DbBlendSpace.BlendSpace;
						});

					if (bBlendSpaceFound)
					{
						InFunction(PoseSearchDb);
					}
				});
		}
		else if (UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(Object))
		{
			ForEachPoseSearchDatabase(bUseTasksDatabases, [Schema, InFunction](UPoseSearchDatabase& PoseSearchDb)
				{
					if (PoseSearchDb.Schema == Schema)
					{
						InFunction(PoseSearchDb);
					}
				});
		}
		else if (USkeleton* Skeleton = Cast<USkeleton>(Object))
		{
			ForEachPoseSearchDatabase(bUseTasksDatabases, [Skeleton, InFunction](UPoseSearchDatabase& PoseSearchDb)
				{
					if (PoseSearchDb.Schema && PoseSearchDb.Schema->Skeleton == Skeleton)
					{
						InFunction(PoseSearchDb);
					}
				});
		}
		else if (UPoseSearchDatabase* PoseSearchDb = Cast<UPoseSearchDatabase>(Object))
		{
			InFunction(*PoseSearchDb);
		}
	}

	// @todo: probably overkilling listening to OnObjectPreSave to RequestAsyncBuildIndex, since we already perform it during OnObjectPropertyChanged
	void FAsyncPoseSearchDatabasesManagement::OnObjectPreSave(UObject* SavedObject, FObjectPreSaveContext SaveContext)
	{
		ExecuteIfObjectIsReferencedByDatabase(SavedObject, false, [this](UPoseSearchDatabase& Database)
			{
				RequestAsyncBuildIndex(Database, false, true);
			});
	}

	// we're listening to OnPreObjectPropertyChanged to cancel any pending Task indexing databases to avoid multi threading issues
	void FAsyncPoseSearchDatabasesManagement::OnPreObjectPropertyChanged(UObject* Object, const class FEditPropertyChain& PropChain)
	{
		ExecuteIfObjectIsReferencedByDatabase(Object, true, [this](UPoseSearchDatabase& Database)
			{
				// cancelling the async indexing request for Database
				WaitOnExistingBuildIndex(Database, false);
			});
	}

	// @todo: investigate if it's possible to move the indexing request then databases index gets accessed
	void FAsyncPoseSearchDatabasesManagement::OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event)
	{
		ExecuteIfObjectIsReferencedByDatabase(Object, false, [this](UPoseSearchDatabase& Database)
			{
				// requesting a new async indexing for Database (it should already have been cancelled by OnPreObjectPropertyChanged)
				RequestAsyncBuildIndex(Database, false, false);
			});
	}

	void FAsyncPoseSearchDatabasesManagement::Shutdown()
	{
		check(IsInGameThread());

		FCoreUObjectDelegates::OnObjectPreSave.Remove(OnObjectPreSaveHandle);
		OnObjectPreSaveHandle.Reset();

		FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(OnPreObjectPropertyChangedHandle);
		OnPreObjectPropertyChangedHandle.Reset();

		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnObjectPropertyChangedHandle);
		OnObjectPropertyChangedHandle.Reset();
	}

	void FAsyncPoseSearchDatabasesManagement::Tick(float DeltaTime)
	{
		check(IsInGameThread());

		const int32 NumTasks = Tasks.Num();
		if (NumTasks > 0)
		{
			// iterating backwards because of the possible RemoveAtSwap 
			for (int32 TaskIndex = NumTasks - 1; TaskIndex >= 0; --TaskIndex)
			{
				FPoseSearchDatabaseAsyncCacheTask& Task = GetTask(TaskIndex);
				if (Task.Poll())
				{
					RemoveTask(TaskIndex);
				}
			}
		}
	}

	void FAsyncPoseSearchDatabasesManagement::TickCook(float DeltaTime, bool bCookCompete)
	{
		Tick(DeltaTime);
	}

	TStatId FAsyncPoseSearchDatabasesManagement::GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncPoseSearchDatabasesManagement, STATGROUP_Tickables);
	}

	void FAsyncPoseSearchDatabasesManagement::AddReferencedObjects(FReferenceCollector& Collector)
	{
	}

	void FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(UPoseSearchDatabase& Database, bool bWaitForCompletion, bool bCancelPreviousTask)
	{
		check(IsInGameThread());

		WaitOnExistingBuildIndex(Database, !bCancelPreviousTask);

#if DO_CHECK
		for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
		{
			const FPoseSearchDatabaseAsyncCacheTask& Task = GetTask(TaskIndex);
			if (&Task.GetDatabase() == &Database)
			{
				// making sure we have no tasks associated to Database
				checkNoEntry();
			}
		}

#endif // DO_CHECK
		Tasks.Emplace(MakeUnique<FPoseSearchDatabaseAsyncCacheTask>(Database));

		if (bWaitForCompletion)
		{
			WaitOnExistingBuildIndex(Database, true);
		}
	}

	void FAsyncPoseSearchDatabasesManagement::WaitOnExistingBuildIndex(const UPoseSearchDatabase& Database, bool bWantResults)
	{
		check(IsInGameThread());

		// iterating backwards because of the possible RemoveAtSwap
		for (int32 TaskIndex = Tasks.Num() - 1; TaskIndex >= 0; --TaskIndex)
		{
			FPoseSearchDatabaseAsyncCacheTask& Task = GetTask(TaskIndex);
			if (&Task.GetDatabase() == &Database)
			{
				if (bWantResults)
				{
					Task.Wait();
				}
				else
				{
					Task.Cancel();
				}

				RemoveTask(TaskIndex);
			}
		}
	}

	bool FAsyncPoseSearchDatabasesManagement::IsBuildingIndex(const UPoseSearchDatabase& Database) const
	{
		for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
		{
			const FPoseSearchDatabaseAsyncCacheTask& Task = GetTask(TaskIndex);
			if (&Task.GetDatabase() == &Database)
			{
				if (!Task.Poll())
				{
					return true;
				}
			}
		}
		return false;
	}

} // namespace UE::PoseSearch
#endif // WITH_EDITOR
