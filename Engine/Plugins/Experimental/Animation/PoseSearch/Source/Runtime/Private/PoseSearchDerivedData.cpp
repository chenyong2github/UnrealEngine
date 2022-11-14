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

	//////////////////////////////////////////////////////////////////////////
	// FPoseSearchDatabaseAsyncCacheTask
	struct FPoseSearchDatabaseAsyncCacheTask
	{
		enum class EState
		{
			Prestarted,
			Cancelled,
			Ended,
			Failed
		};

		// these methods MUST be protected by FPoseSearchDatabaseAsyncCacheTask::Mutex! and to make sure we pass the mutex as input param
		FPoseSearchDatabaseAsyncCacheTask(UPoseSearchDatabase* InDatabase, FCriticalSection& OuterMutex);
		void StartNewRequestIfNeeded(FCriticalSection& OuterMutex);
		bool CancelIfDependsOn(const UObject* Object, FCriticalSection& OuterMutex);
		void Update(FCriticalSection& OuterMutex);
		void Wait(FCriticalSection& OuterMutex);
		void Cancel(FCriticalSection& OuterMutex);
		bool Poll(FCriticalSection& OuterMutex) const;
		bool ContainsDatabase(const UPoseSearchDatabase* OtherDatabase, FCriticalSection& OuterMutex) const;

		~FPoseSearchDatabaseAsyncCacheTask();
		EState GetState() const { return EState(ThreadSafeState.GetValue()); }

	private:
		FPoseSearchDatabaseAsyncCacheTask(const FPoseSearchDatabaseAsyncCacheTask& Other) = delete;
		FPoseSearchDatabaseAsyncCacheTask(FPoseSearchDatabaseAsyncCacheTask&& Other) = delete;
		FPoseSearchDatabaseAsyncCacheTask& operator=(const FPoseSearchDatabaseAsyncCacheTask& Other) = delete;
		FPoseSearchDatabaseAsyncCacheTask& operator=(FPoseSearchDatabaseAsyncCacheTask&& Other) = delete;

		void OnGetComplete(UE::DerivedData::FCacheGetResponse&& Response);
		void SetState(EState State) { ThreadSafeState.Set(int32(State)); }

		TWeakObjectPtr<UPoseSearchDatabase> Database;
		// @todo: this is not relevant when the async task is completed, so to save memory we should move it as pointer perhaps
		FPoseSearchIndex SearchIndex;
		UE::DerivedData::FRequestOwner Owner;
		FIoHash DerivedDataKey = FIoHash::Zero;
		TSet<TWeakObjectPtr<const UObject>> DatabaseDependencies; // @todo: make this const
		
		FThreadSafeCounter ThreadSafeState = int32(EState::Prestarted);
		bool bBroadcastOnDerivedDataRebuild = false;
	};

	class FPoseSearchDatabaseAsyncCacheTasks : public TArray<TUniquePtr<FPoseSearchDatabaseAsyncCacheTask>> {};

	FPoseSearchDatabaseAsyncCacheTask::FPoseSearchDatabaseAsyncCacheTask(UPoseSearchDatabase* InDatabase, FCriticalSection& OuterMutex)
		: Database(InDatabase)
		, Owner(UE::DerivedData::EPriority::Normal)
	{
		StartNewRequestIfNeeded(OuterMutex);
	}

	FPoseSearchDatabaseAsyncCacheTask::~FPoseSearchDatabaseAsyncCacheTask()
	{
		Owner.Cancel();
	}

	void FPoseSearchDatabaseAsyncCacheTask::StartNewRequestIfNeeded(FCriticalSection& OuterMutex)
	{
		using namespace UE::DerivedData;

		FScopeLock Lock(&OuterMutex);

		// making sure there are no active requests
		Owner.Cancel();

		// composing the key
		FDerivedDataKeyBuilder KeyBuilder;
		FGuid VersionGuid = FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().POSESEARCHDB_DERIVEDDATA_VER);
		UPoseSearchDatabase* Db = Database.Get();
		check(Db);
		KeyBuilder << VersionGuid;
		KeyBuilder << Db;

		// Stores a BLAKE3-160 hash, taken from the first 20 bytes of a BLAKE3-256 hash
		const FIoHash NewDerivedDataKey = KeyBuilder.Finalize();
		const bool bHasKeyChanged = NewDerivedDataKey != DerivedDataKey;
		if (bHasKeyChanged)
		{
			DerivedDataKey = NewDerivedDataKey;

			DatabaseDependencies.Reset();
			for (const UObject* Dependency : KeyBuilder.GetDependencies())
			{
				DatabaseDependencies.Add(Dependency);
			}

			SetState(EState::Prestarted);

			UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BeginCache"), *LexToString(DerivedDataKey), *Database->GetName());

			TArray<FCacheGetRequest> CacheRequests;
			const FCacheKey CacheKey{ Bucket, DerivedDataKey };
			CacheRequests.Add({ { Database->GetPathName() }, CacheKey, ECachePolicy::Default });

			Owner = FRequestOwner(EPriority::Normal);
			GetCache().Get(CacheRequests, Owner, [this](FCacheGetResponse&& Response)
				{
					OnGetComplete(MoveTemp(Response));
				});
		}
	}

	// it cancels and waits for the task to be done and reset the local SearchIndex. SetState to Cancelled
	void FPoseSearchDatabaseAsyncCacheTask::Cancel(FCriticalSection& OuterMutex)
	{
		FScopeLock Lock(&OuterMutex);

		Owner.Cancel();
		check(GetState() == EState::Prestarted);
		SearchIndex.Reset();
		SetState(EState::Cancelled);
	}

	bool FPoseSearchDatabaseAsyncCacheTask::CancelIfDependsOn(const UObject* Object, FCriticalSection& OuterMutex)
	{
		FScopeLock Lock(&OuterMutex);

		// DatabaseDependencies is updated only in StartNewRequestIfNeeded when there are no active requests, so it's thread safe to access it 
		if (DatabaseDependencies.Contains(Object))
		{
			if (GetState() == EState::Prestarted)
			{
				Cancel(OuterMutex);
			}
			else
			{
				SearchIndex.Reset();
				SetState(EState::Cancelled);
			}
			return true;
		}
		return false;
	}

	void FPoseSearchDatabaseAsyncCacheTask::Update(FCriticalSection& OuterMutex)
	{
		check(IsInGameThread());

		FScopeLock Lock(&OuterMutex);

		check(GetState() != EState::Cancelled); // otherwise FPoseSearchDatabaseAsyncCacheTask should have been already removed

		if (GetState() == EState::Prestarted && Poll(OuterMutex))
		{
			// task is done: we need to update the state form Prestarted to Ended/Failed
			Wait(OuterMutex);
		}

		if (bBroadcastOnDerivedDataRebuild)
		{
			Database->NotifyDerivedDataRebuild();
			bBroadcastOnDerivedDataRebuild = false;
		}
	}

	// it waits for the task to be done and SetSearchIndex on the database. SetState to Ended/Failed
	void FPoseSearchDatabaseAsyncCacheTask::Wait(FCriticalSection& OuterMutex)
	{
		check(GetState() == EState::Prestarted);

		Owner.Wait();

		FScopeLock Lock(&OuterMutex);

		const bool bFailedIndexing = SearchIndex.IsEmpty();
		if (!bFailedIndexing)
		{
			Database->SetSearchIndex(SearchIndex); // @todo: implement FPoseSearchIndex move ctor and assignment operator and use a MoveTemp(SearchIndex) here
			SetState(EState::Ended);
			bBroadcastOnDerivedDataRebuild = true;
		}
		else
		{
			check(!bBroadcastOnDerivedDataRebuild);
			SetState(EState::Failed);
		}
		SearchIndex.Reset();
	}

	// true is the task is done executing
	bool FPoseSearchDatabaseAsyncCacheTask::Poll(FCriticalSection& OuterMutex) const
	{
		return Owner.Poll();
	}

	bool FPoseSearchDatabaseAsyncCacheTask::ContainsDatabase(const UPoseSearchDatabase* OtherDatabase, FCriticalSection& OuterMutex) const
	{
		FScopeLock Lock(&OuterMutex);
		return Database.Get() == OtherDatabase;
	}

	// called once the task is done:
	// if EStatus::Ok (data has been retrieved from DDC) we deserialize the payload into the local SearchIndex
	// if EStatus::Error we BuildIndex and if that's successful we 'Put' it on DDC
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

			UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex From Cache"), *LexToString(Key.Hash), *Database->GetName());

			COOK_STAT(Timer.AddHit(RawData.GetSize()));
		}
		else if (Response.Status == EStatus::Error)
		{
			// we didn't find the cached data associated to the PendingDerivedDataKey: we'll BuildIndex to update SearchIndex and "Put" the data over the DDC
			Owner.LaunchTask(TEXT("PoseSearchDatabaseBuild"), [this, Key]
				{
					COOK_STAT(auto Timer = UsageStats.TimeSyncWork());

					int32 BytesProcessed = 0;
					if (BuildIndex(*Database, SearchIndex, Owner))
					{
						UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Succeeded"), *LexToString(Key.Hash), *Database->GetName());

						TArray<uint8> RawBytes;
						FMemoryWriter Writer(RawBytes);
						Writer << SearchIndex;
						FSharedBuffer RawData = MakeSharedBufferFromArray(MoveTemp(RawBytes));
						BytesProcessed = RawData.GetSize();

						FCacheRecordBuilder Builder(Key);
						Builder.AddValue(Id, RawData);
						GetCache().Put({ { { Database->GetPathName() }, Builder.Build() } }, Owner, [this, Key](FCachePutResponse&& Response)
							{
								if (Response.Status == EStatus::Error)
								{
									UE_LOG(LogPoseSearch, Log, TEXT("%s - %s Failed to store DDC"), *LexToString(Key.Hash), *Database->GetName());
								}
							});
					}
					else
					{
						if (Owner.IsCanceled())
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(Key.Hash), *Database->GetName());
						}
						else
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Failed"), *LexToString(Key.Hash), *Database->GetName());
						}
						SearchIndex.Reset();
					}

					COOK_STAT(Timer.AddMiss(BytesProcessed));
				});
		}
		else if(Response.Status == EStatus::Canceled)
		{
			SearchIndex.Reset();
			UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(Key.Hash), *Database->GetName());
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// FAsyncPoseSearchDatabasesManagement
	FCriticalSection FAsyncPoseSearchDatabasesManagement::Mutex;

	FAsyncPoseSearchDatabasesManagement& FAsyncPoseSearchDatabasesManagement::Get()
	{
		FScopeLock Lock(&Mutex);

		static FAsyncPoseSearchDatabasesManagement SingletonInstance;
		return SingletonInstance;
	}

	FAsyncPoseSearchDatabasesManagement::FAsyncPoseSearchDatabasesManagement()
		: Tasks(*(new FPoseSearchDatabaseAsyncCacheTasks()))
	{
		FScopeLock Lock(&Mutex);

		OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::OnObjectModified);
		FCoreDelegates::OnPreExit.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::Shutdown);
	}

	FAsyncPoseSearchDatabasesManagement::~FAsyncPoseSearchDatabasesManagement()
	{
		FScopeLock Lock(&Mutex);

		FCoreDelegates::OnPreExit.RemoveAll(this);
		Shutdown();

		delete &Tasks;
	}

	// we're listening to OnObjectModified to cancel any pending Task indexing databases depending from Object to avoid multi threading issues
	void FAsyncPoseSearchDatabasesManagement::OnObjectModified(UObject* Object)
	{
		FScopeLock Lock(&Mutex);

		// iterating backwards because of the possible RemoveAtSwap
		for (int32 TaskIndex = Tasks.Num() - 1; TaskIndex >= 0; --TaskIndex)
		{
			if (Tasks[TaskIndex]->CancelIfDependsOn(Object, Mutex))
			{
				Tasks.RemoveAtSwap(TaskIndex, 1, false);
			}
		}
	}

	void FAsyncPoseSearchDatabasesManagement::Shutdown()
	{
		FScopeLock Lock(&Mutex);

		FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);
		OnObjectModifiedHandle.Reset();
	}

	void FAsyncPoseSearchDatabasesManagement::Tick(float DeltaTime)
	{
		FScopeLock Lock(&Mutex);

		check(IsInGameThread());

		// iterating backwards because of the possible RemoveAtSwap 
		for (int32 TaskIndex = Tasks.Num() - 1; TaskIndex >= 0; --TaskIndex)
		{
			Tasks[TaskIndex]->Update(Mutex);
			
			// @todo: check key validity every few ticks, or perhaps delete unused for a long time Tasks
		}
	}

	void FAsyncPoseSearchDatabasesManagement::TickCook(float DeltaTime, bool bCookCompete)
	{
		FScopeLock Lock(&Mutex);

		Tick(DeltaTime);
	}

	TStatId FAsyncPoseSearchDatabasesManagement::GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncPoseSearchDatabasesManagement, STATGROUP_Tickables);
	}

	void FAsyncPoseSearchDatabasesManagement::AddReferencedObjects(FReferenceCollector& Collector)
	{
	}

	// returns true if the index has been built and the Database updated correctly  
	bool FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(const UPoseSearchDatabase* Database, ERequestAsyncBuildFlag Flag)
	{
		if (!Database)
		{
			return false;
		}

		FScopeLock Lock(&Mutex);

		check(Database);
		check(EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::ContinueRequest));

		FAsyncPoseSearchDatabasesManagement& This = FAsyncPoseSearchDatabasesManagement::Get();

		FPoseSearchDatabaseAsyncCacheTask* Task = nullptr;
		for (TUniquePtr<FPoseSearchDatabaseAsyncCacheTask>& TaskPtr : This.Tasks)
		{
			if (TaskPtr->ContainsDatabase(Database, Mutex))
			{
				Task = TaskPtr.Get();

				if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::NewRequest))
				{
					if (Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Prestarted)
					{
						if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::WaitPreviousRequest))
						{
							Task->Wait(Mutex);
						}
						else
						{
							Task->Cancel(Mutex);
						}
					}

					Task->StartNewRequestIfNeeded(Mutex);
				}
				else // if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::ContinueRequest))
				{
					if (Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Prestarted)
					{
						if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::WaitPreviousRequest))
						{
							Task->Wait(Mutex);
						}
					}
				}
				break;
			}
		}
		
		if (!Task)
		{
			// we didn't find the Task, so we Emplace a new one
			This.Tasks.Emplace(MakeUnique<FPoseSearchDatabaseAsyncCacheTask>(const_cast<UPoseSearchDatabase*>(Database), Mutex));
			Task = This.Tasks.Last().Get();
		}

		if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::WaitForCompletion) && Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Prestarted)
		{
			Task->Wait(Mutex);
		}

		return Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Ended;
	}

} // namespace UE::PoseSearch
#endif // WITH_EDITOR
