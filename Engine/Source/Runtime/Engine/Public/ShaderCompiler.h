// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompiler.h: Platform independent shader compilation definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"
#include "HAL/PlatformProcess.h"
#include "ShaderCore.h"
#include "ShaderCompilerCore.h"
#include "Shader.h"
#include "HAL/RunnableThread.h"
#include "HAL/Runnable.h"
#include "Templates/Atomic.h"
#include "Templates/UniquePtr.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/ScopeRWLock.h"
#include "Containers/HashTable.h"
#include "Containers/List.h"

class FShaderCompileJob;
class FShaderPipelineCompileJob;
class FVertexFactoryType;
class IDistributedBuildController;
class FMaterialShaderMap;

DECLARE_LOG_CATEGORY_EXTERN(LogShaderCompilers, Log, All);

class FShaderCompileJob;
class FShaderPipelineCompileJob;

#define DEBUG_INFINITESHADERCOMPILE 0

enum class EShaderCompilerWorkerType : uint8
{
	None,
	LocalThread,
	XGE,
};

enum class EShaderCompileJobType : uint8
{
	Single,
	Pipeline,
	Num,
};
static const int32 NumShaderCompileJobTypes = (int32)EShaderCompileJobType::Num;

enum class EShaderCompileJobPriority : uint8
{
	None = 0xff,

	Low = 0u,
	Normal,
	High,
	ForceLocal, // Force shader to skip XGE and compile on local machine
	Num,
};
static const int32 NumShaderCompileJobPriorities = (int32)EShaderCompileJobPriority::Num;

inline const TCHAR* ShaderCompileJobPriorityToString(EShaderCompileJobPriority v)
{
	switch (v)
	{
	case EShaderCompileJobPriority::None: return TEXT("None");
	case EShaderCompileJobPriority::Low: return TEXT("Low");
	case EShaderCompileJobPriority::Normal: return TEXT("Normal");
	case EShaderCompileJobPriority::High: return TEXT("High");
	case EShaderCompileJobPriority::ForceLocal: return TEXT("ForceLocal");
	default: checkNoEntry(); return TEXT("");
	}
}

/** Results for a single compiled shader map. */
struct FShaderMapCompileResults
{
	FShaderMapCompileResults() :
		bAllJobsSucceeded(true),
		bSkipResultProcessing(false),
		TimeStarted(FPlatformTime::Seconds()),
		bIsHung(false)
	{}

	void CheckIfHung();

	TArray<TRefCountPtr<class FShaderCommonCompileJob>> FinishedJobs;
	FThreadSafeCounter NumPendingJobs;
	bool bAllJobsSucceeded;
	bool bSkipResultProcessing;
	double TimeStarted;
	bool bIsHung;
};

struct FPendingShaderMapCompileResults
	: public FShaderMapCompileResults
	, public FRefCountBase
{};
using FPendingShaderMapCompileResultsPtr = TRefCountPtr<FPendingShaderMapCompileResults>;

/** Stores all of the common information used to compile a shader or pipeline. */
class FShaderCommonCompileJob : public TIntrusiveLinkedList<FShaderCommonCompileJob>
{
public:
	FPendingShaderMapCompileResultsPtr PendingShaderMap;

	mutable FThreadSafeCounter NumRefs;
	int32 JobIndex;
	uint32 Hash;

	/** Id of the shader map this shader belongs to. */
	uint32 Id;

	EShaderCompileJobType Type;
	EShaderCompileJobPriority Priority;
	EShaderCompileJobPriority PendingPriority;
	EShaderCompilerWorkerType CurrentWorker;

	/** true if the results of the shader compile have been processed. */
	uint8 bFinalized : 1;
	/** Output of the shader compile */
	uint8 bSucceeded : 1;
	/** true if the results of the shader compile have been released from the FShaderCompilerManager.
		After a job is bFinalized it will be bReleased when ReleaseJob() is invoked, which means that the shader compile thread
		is no longer processing the job; which is useful for non standard job handling (Niagara as an example). */
	uint8 bReleased : 1;

	/** Whether we hashed the inputs */
	uint8 bInputHashSet : 1;
	/** Hash of all the job inputs */
	FSHAHash InputHash;

	uint32 AddRef() const
	{
		return uint32(NumRefs.Increment());
	}

	uint32 Release() const
	{
		uint32 Refs = uint32(NumRefs.Decrement());
		if (Refs == 0)
		{
			Destroy();
		}
		return Refs;
	}
	uint32 GetRefCount() const
	{
		return uint32(NumRefs.GetValue());
	}

	/** Returns hash of all inputs for this job (needed for caching). */
	virtual FSHAHash GetInputHash() { return FSHAHash(); }

	/** Serializes (and deserializes) the output for caching purposes. */
	virtual void SerializeOutput(FArchive& Ar) {}

	FShaderCompileJob* GetSingleShaderJob();
	const FShaderCompileJob* GetSingleShaderJob() const;
	FShaderPipelineCompileJob* GetShaderPipelineJob();
	const FShaderPipelineCompileJob* GetShaderPipelineJob() const;

	bool Equals(const FShaderCommonCompileJob& Rhs) const;
	
	/** This returns a unique id for a shader compiler job */
	ENGINE_API static uint32 GetNextJobId();

protected:
	friend class FShaderCompilingManager;
	friend class FShaderPipelineCompileJob;

	FShaderCommonCompileJob(EShaderCompileJobType InType, uint32 InHash, uint32 InId, EShaderCompileJobPriority InPriroity) :
		NumRefs(0),
		JobIndex(INDEX_NONE),
		Hash(InHash),
		Id(InId),
		Type(InType),
		Priority(InPriroity),
		PendingPriority(EShaderCompileJobPriority::None),
		CurrentWorker(EShaderCompilerWorkerType::None),
		bFinalized(false),
		bSucceeded(false),
		bReleased(false),
		bInputHashSet(false)
	{
		check(InPriroity != EShaderCompileJobPriority::None);
	}

	virtual ~FShaderCommonCompileJob() {}

private:
	/** Value counter for job ids. */
	static FThreadSafeCounter JobIdCounter;

	void Destroy() const;
};
using FShaderCommonCompileJobPtr = TRefCountPtr<FShaderCommonCompileJob>;

struct FShaderCompileJobKey
{
	explicit FShaderCompileJobKey(const FShaderType* InType = nullptr, const FVertexFactoryType* InVFType = nullptr, int32 InPermutationId = 0)
		: ShaderType(InType), VFType(InVFType), PermutationId(InPermutationId)
	{}

	uint32 MakeHash(uint32 Id) const { return HashCombine(HashCombine(HashCombine(GetTypeHash(Id), GetTypeHash(VFType)), GetTypeHash(ShaderType)), GetTypeHash(PermutationId)); }

	const FShaderType* ShaderType;
	const FVertexFactoryType* VFType;
	int32 PermutationId;
};
inline bool operator==(const FShaderCompileJobKey& Lhs, const FShaderCompileJobKey& Rhs)
{
	return Lhs.VFType == Rhs.VFType && Lhs.ShaderType == Rhs.ShaderType && Lhs.PermutationId == Rhs.PermutationId;
}
inline bool operator!=(const FShaderCompileJobKey& Lhs, const FShaderCompileJobKey& Rhs)
{
	return !operator==(Lhs, Rhs);
}

/** Stores all of the input and output information used to compile a single shader. */
class FShaderCompileJob : public FShaderCommonCompileJob
{
public:
	static const EShaderCompileJobType Type = EShaderCompileJobType::Single;

	FShaderCompileJobKey Key;
	/** Input for the shader compile */
	FShaderCompilerInput Input;
	FShaderCompilerOutput Output;

	// List of pipelines that are sharing this job.
	TMap<const FVertexFactoryType*, TArray<const FShaderPipelineType*>> SharingPipelines;

	virtual ENGINE_API FSHAHash GetInputHash() override;
	virtual ENGINE_API void SerializeOutput(FArchive& Ar) override;

	FShaderCompileJob(uint32 InHash, uint32 InId, EShaderCompileJobPriority InPriroity, const FShaderCompileJobKey& InKey) :
		FShaderCommonCompileJob(Type, InHash, InId, InPriroity),
		Key(InKey)
	{}
};

struct FShaderPipelineCompileJobKey
{
	explicit FShaderPipelineCompileJobKey(const FShaderPipelineType* InType = nullptr, const FVertexFactoryType* InVFType = nullptr, int32 InPermutationId = 0)
		: ShaderPipeline(InType), VFType(InVFType), PermutationId(InPermutationId)
	{}

	uint32 MakeHash(uint32 Id) const { return HashCombine(HashCombine(HashCombine(GetTypeHash(Id), GetTypeHash(ShaderPipeline)), GetTypeHash(VFType)), GetTypeHash(PermutationId)); }

	const FShaderPipelineType* ShaderPipeline;
	const FVertexFactoryType* VFType;
	int32 PermutationId;
};
inline bool operator==(const FShaderPipelineCompileJobKey& Lhs, const FShaderPipelineCompileJobKey& Rhs)
{
	return Lhs.ShaderPipeline == Rhs.ShaderPipeline && Lhs.VFType == Rhs.VFType && Lhs.PermutationId == Rhs.PermutationId;
}
inline bool operator!=(const FShaderPipelineCompileJobKey& Lhs, const FShaderPipelineCompileJobKey& Rhs)
{
	return !operator==(Lhs, Rhs);
}

class FShaderPipelineCompileJob : public FShaderCommonCompileJob
{
public:
	static const EShaderCompileJobType Type = EShaderCompileJobType::Pipeline;

	FShaderPipelineCompileJobKey Key;
	TArray<TRefCountPtr<FShaderCompileJob>> StageJobs;
	bool bFailedRemovingUnused;

	virtual ENGINE_API FSHAHash GetInputHash() override;
	virtual ENGINE_API void SerializeOutput(FArchive& Ar) override;

	FShaderPipelineCompileJob(uint32 InHash, uint32 InId, EShaderCompileJobPriority InPriroity, const FShaderPipelineCompileJobKey& InKey);
};

inline FShaderCompileJob* FShaderCommonCompileJob::GetSingleShaderJob() { return Type == EShaderCompileJobType::Single ? static_cast<FShaderCompileJob*>(this) : nullptr; }
inline const FShaderCompileJob* FShaderCommonCompileJob::GetSingleShaderJob() const { return Type == EShaderCompileJobType::Single ? static_cast<const FShaderCompileJob*>(this) : nullptr; }
inline FShaderPipelineCompileJob* FShaderCommonCompileJob::GetShaderPipelineJob() { return Type == EShaderCompileJobType::Pipeline ? static_cast<FShaderPipelineCompileJob*>(this) : nullptr; }
inline const FShaderPipelineCompileJob* FShaderCommonCompileJob::GetShaderPipelineJob() const { return Type == EShaderCompileJobType::Pipeline ? static_cast<const FShaderPipelineCompileJob*>(this) : nullptr; }

inline bool FShaderCommonCompileJob::Equals(const FShaderCommonCompileJob& Rhs) const
{
	if (Type == Rhs.Type && Id == Rhs.Id)
	{
		switch (Type)
		{
		case EShaderCompileJobType::Single: return static_cast<const FShaderCompileJob*>(this)->Key == static_cast<const FShaderCompileJob&>(Rhs).Key;
		case EShaderCompileJobType::Pipeline: return static_cast<const FShaderPipelineCompileJob*>(this)->Key == static_cast<const FShaderPipelineCompileJob&>(Rhs).Key;
		default: checkNoEntry(); break;
		}
	}
	return false;
}

inline void FShaderCommonCompileJob::Destroy() const
{
	switch (Type)
	{
	case EShaderCompileJobType::Single: delete static_cast<const FShaderCompileJob*>(this); break;
	case EShaderCompileJobType::Pipeline: delete static_cast<const FShaderPipelineCompileJob*>(this); break;
	default: checkNoEntry();
	}
}

class FShaderJobCache
{
public:
	using FJobInputHash = FSHAHash;
	using FJobCachedOutput = TArray<uint8>;

	/** Looks for the job in the cache, returns null if not found */
	FJobCachedOutput* Find(const FJobInputHash& Hash);

	/** Adds a job output to the cache */
	void Add(const FJobInputHash& Hash, const FJobCachedOutput& Contents, int InitialHitCount);

	/** Calculates memory used by the cache*/
	uint64 GetAllocatedMemory();

	/** Logs out the statistics */
	void LogStats();

	/** Calculates current memory budget, in bytes */
	uint64 GetCurrentMemoryBudget() const;

private:
	using FJobOutputHash = FSHAHash;
	struct FStoredOutput
	{
		/** How many times this output is referenced by the cached jobs */
		int32 NumReferences;

		/** How many times this output has been returned as a cached result, no matter the input hash */
		int32 NumHits;
		
		/** Canned output */
		TArray<uint8> JobOutput;
	};

	/* a lot of outputs can be duplicated, so they are deduplicated before storing */
	TMap<FJobOutputHash, FStoredOutput*> Outputs;

	/** Map of input hashes to output hashes */
	TMap<FJobInputHash, FJobOutputHash> InputHashToOutput;

	/** Statistics - total number of times we tried to Find() some input hash */
	uint64 TotalSearchAttempts = 0;

	/** Statistics - total number of times we succeded in Find()ing output for some input hash */
	uint64 TotalCacheHits = 0;

	/** Statistics - allocated memory. If the number is non-zero, we can trust it as accurate. Otherwise, recalculate. */
	uint64 CurrentlyAllocatedMemory = 0;
};


class FShaderCompileJobCollection
{
public:
	FShaderCompileJobCollection();

	FShaderCompileJob* PrepareJob(uint32 InId, const FShaderCompileJobKey& InKey, EShaderCompileJobPriority InPriority);
	FShaderPipelineCompileJob* PrepareJob(uint32 InId, const FShaderPipelineCompileJobKey& InKey, EShaderCompileJobPriority InPriority);
	void RemoveJob(FShaderCommonCompileJob* InJob);

	int32 RemoveAllPendingJobsWithId(uint32 InId);

	void SubmitJobs(const TArray<FShaderCommonCompileJobPtr>& InJobs);
	
	/** This is an entry point for all jobs that have finished the compilation (whether real or cached). Can be called from multiple threads.*/
	void ProcessFinishedJob(FShaderCommonCompileJob* FinishedJob, bool bWasCached = false);

	/** Adds the job to cache. */
	void AddToCacheAndProcessPending(FShaderCommonCompileJob* FinishedJob);

	/** Log caching statistics.
	 *
	 * @param bForceLogIgnoringTimeInverval - this function is called often, so not every invocation normally will actually log the stats. This parameter being true bypasses this pacing.
	 */
	void LogCachingStats(bool bForceLogIgnoringTimeInverval = false);

	inline int32 GetNumPendingJobs(EShaderCompileJobPriority InPriority) const
	{
		return NumPendingJobs[(int32)InPriority];
	}

	inline int32 GetNumOutstandingJobs() const
	{
		return NumOutstandingJobs.GetValue();
	}

	int32 GetNumPendingJobs() const;

	int32 GetPendingJobs(EShaderCompilerWorkerType InWorkerType, EShaderCompileJobPriority InPriority, int32 MinNumJobs, int32 MaxNumJobs, TArray<FShaderCommonCompileJobPtr>& OutJobs);

private:
	void InternalAddJob(FShaderCommonCompileJob* Job);
	void InternalRemoveJob(FShaderCommonCompileJob* InJob);
	void InternalSetPriority(FShaderCommonCompileJob* Job, EShaderCompileJobPriority InPriority);
	// cannot allow managing this from outside as the caching logic is not exposed
	inline int32 InternalSubtractNumOutstandingJobs(int32 Value)
	{
		const int32 PrevNumOutstandingJobs = NumOutstandingJobs.Subtract(Value);
		check(PrevNumOutstandingJobs >= Value);
		return PrevNumOutstandingJobs - Value;
	}

	template<typename JobType, typename KeyType>
	int32 InternalFindJobIndex(uint32 InJobHash, uint32 InJobId, const KeyType& InKey) const
	{
		const int32 TypeIndex = (int32)JobType::Type;
		uint32 CurrentPriorityIndex = 0u;
		int32 CurrentIndex = INDEX_NONE;
		for (int32 Index = JobHash[TypeIndex].First(InJobHash); JobHash[TypeIndex].IsValid(Index); Index = JobHash[TypeIndex].Next(Index))
		{
			const FShaderCommonCompileJob* Job = Jobs[TypeIndex][Index].GetReference();
			check(Job->Type == JobType::Type);

			// We find the job that matches the key with the highest priority
			if (Job->Id == InJobId &&
				(uint32)Job->Priority >= CurrentPriorityIndex &&
				static_cast<const JobType*>(Job)->Key == InKey)
			{
				CurrentPriorityIndex = (uint32)Job->Priority;
				CurrentIndex = Index;
			}
		}
		return CurrentIndex;
	}

	template<typename JobType, typename KeyType>
	JobType* InternalFindJob(uint32 InJobHash, uint32 InJobId, const KeyType& InKey) const
	{
		const int32 TypeIndex = (int32)JobType::Type;
		const int32 JobIndex = InternalFindJobIndex<JobType>(InJobHash, InJobId, InKey);
		return JobIndex != INDEX_NONE ? static_cast<JobType*>(Jobs[TypeIndex][JobIndex].GetReference()) : nullptr;
	}

	template<typename JobType, typename KeyType>
	JobType* InternalPrepareJob(uint32 InId, const KeyType& InKey, EShaderCompileJobPriority InPriority)
	{
		const uint32 Hash = InKey.MakeHash(InId);
		JobType* PrevJob = nullptr;
		{
			FReadScopeLock Locker(Lock);
			PrevJob = InternalFindJob<JobType>(Hash, InId, InKey);
		}

		JobType* NewJob = nullptr;
		if (PrevJob == nullptr || (uint32)InPriority > (uint32)PrevJob->Priority)
		{
			FWriteScopeLock Locker(Lock);
			if (PrevJob == nullptr)
			{
				PrevJob = InternalFindJob<JobType>(Hash, InId, InKey);
			}
			if (PrevJob == nullptr)
			{
				NewJob = new JobType(Hash, InId, InPriority, InKey);
				InternalAddJob(NewJob);
			}
			else if ((uint32)InPriority > (uint32)PrevJob->Priority)
			{
				InternalSetPriority(PrevJob, InPriority);
			}
		}

		return NewJob;
	}

	/** Handles the console command to log jobs cache stats */
	void HandleLogJobsCacheStats();

	/** Queue of tasks that haven't been assigned to a worker yet. */
	FShaderCommonCompileJob* PendingJobs[NumShaderCompileJobPriorities];
	int32 NumPendingJobs[NumShaderCompileJobPriorities];

	/** Number of jobs currently being compiled.  This includes PendingJobs and any jobs that have been assigned to workers but aren't complete yet. */
	FThreadSafeCounter NumOutstandingJobs;

	TArray<FShaderCommonCompileJobPtr> Jobs[NumShaderCompileJobTypes];
	TArray<int32> FreeIndices[NumShaderCompileJobTypes];
	FHashTable JobHash[NumShaderCompileJobTypes];
	/** Guards access to the above job storage and also the cache structures below - JobsInFlight, WaitList and the Cache itself */
	mutable FRWLock Lock;

	/** Map of input hash to the jobs that we decided to execute. Note that mapping will miss cloned jobs (to avoid being a multimap). */
	TMap<FSHAHash, FShaderCommonCompileJob*> JobsInFlight;

	/** Map of input hash to the jobs that we delayed because a job with the same hash was executing. Each job is a head of a linked list of jobs with the same input hash (ihash) */
	TMap<FSHAHash, FShaderCommonCompileJob*> DuplicateJobsWaitList;

	/** Cache for the completed jobs.*/
	FShaderJobCache CompletedJobsCache;

	/** Debugging - console command to print stats. */
	class IConsoleObject* LogJobsCacheStatsCmd;
};

class FGlobalShaderTypeCompiler
{
public:
	/**
	* Enqueues compilation of a shader of this type.
	*/
	ENGINE_API static void BeginCompileShader(const FGlobalShaderType* ShaderType, int32 PermutationId, EShaderPlatform Platform, EShaderPermutationFlags PermutationFlags, TArray<FShaderCommonCompileJobPtr>& NewJobs);

	/**
	* Enqueues compilation of a shader pipeline of this type.
	*/
	ENGINE_API static void BeginCompileShaderPipeline(EShaderPlatform Platform, EShaderPermutationFlags PermutationFlags, const FShaderPipelineType* ShaderPipeline, TArray<FShaderCommonCompileJobPtr>& NewJobs);

	/** Either returns an equivalent existing shader of this type, or constructs a new instance. */
	static FShader* FinishCompileShader(const FGlobalShaderType* ShaderType, const FShaderCompileJob& CompileJob, const FShaderPipelineType* ShaderPipelineType);
};

class FShaderCompileThreadRunnableBase : public FRunnable
{
	friend class FShaderCompilingManager;

protected:
	/** The manager for this thread */
	class FShaderCompilingManager* Manager;
	/** The runnable thread */
	FRunnableThread* Thread;

	int32 MinPriorityIndex;
	int32 MaxPriorityIndex;
	
	/** If the thread has been terminated by an unhandled exception, this contains the error message. */
	FString ErrorMessage;
	/** true if the thread has been terminated by an unhandled exception. */
	bool bTerminatedByError;

	TAtomic<bool> bForceFinish;

public:
	FShaderCompileThreadRunnableBase(class FShaderCompilingManager* InManager);
	virtual ~FShaderCompileThreadRunnableBase()
	{}

	inline void SetPriorityRange(EShaderCompileJobPriority MinPriority, EShaderCompileJobPriority MaxPriority)
	{
		MinPriorityIndex = (int32)MinPriority;
		MaxPriorityIndex = (int32)MaxPriority;
		check(MaxPriorityIndex >= MinPriorityIndex);
	}

	void StartThread();

	// FRunnable interface.
	virtual void Stop() { bForceFinish = true; }
	virtual uint32 Run();
	inline void WaitForCompletion() const
	{
		if( Thread )
		{
			Thread->WaitForCompletion();
		}
	}

	/** Checks the thread's health, and passes on any errors that have occured.  Called by the main thread. */
	void CheckHealth() const;

	/** Main work loop. */
	virtual int32 CompilingLoop() = 0;
};

/** 
 * Shader compiling thread
 * This runs in the background while UE4 is running, launches shader compile worker processes when necessary, and feeds them inputs and reads back the outputs.
 */
class FShaderCompileThreadRunnable : public FShaderCompileThreadRunnableBase
{
	friend class FShaderCompilingManager;
private:

	/** Information about the active workers that this thread is tracking. */
	TArray<struct FShaderCompileWorkerInfo*> WorkerInfos;
	/** Tracks the last time that this thread checked if the workers were still active. */
	double LastCheckForWorkersTime;

public:
	/** Initialization constructor. */
	FShaderCompileThreadRunnable(class FShaderCompilingManager* InManager);
	virtual ~FShaderCompileThreadRunnable();

private:

	/** 
	 * Grabs tasks from Manager->CompileQueue in a thread safe way and puts them into QueuedJobs of available workers. 
	 * Also writes completed jobs to Manager->ShaderMapJobs.
	 */
	int32 PullTasksFromQueue();

	/** Used when compiling through workers, writes out the worker inputs for any new tasks in WorkerInfos.QueuedJobs. */
	void WriteNewTasks();

	/** Used when compiling through workers, launches worker processes if needed. */
	bool LaunchWorkersIfNeeded();

	/** Used when compiling through workers, attempts to open the worker output file if the worker is done and read the results. */
	void ReadAvailableResults();

	/** Used when compiling directly through the console tools dll. */
	void CompileDirectlyThroughDll();

	/** Main work loop. */
	virtual int32 CompilingLoop() override;
};

namespace FShaderCompileUtilities
{
	bool DoWriteTasks(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& TransferFile, bool bUseRelativePaths = false);
	void DoReadTaskResults(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& OutputFile);

	/** Execute the specified (single or pipeline) shader compile job. */
	void ExecuteShaderCompileJob(FShaderCommonCompileJob& Job);

	class FArchive* CreateFileHelper(const FString& Filename);
	void MoveFileHelper(const FString& To, const FString& From);
	void DeleteFileHelper(const FString& Filename);
}

#if PLATFORM_WINDOWS // XGE shader compilation is only supported on Windows.

class FShaderCompileXGEThreadRunnable_XmlInterface : public FShaderCompileThreadRunnableBase
{
private:
	/** The handle referring to the XGE console process, if a build is in progress. */
	FProcHandle BuildProcessHandle;
	
	/** Process ID of the XGE console, if a build is in progress. */
	uint32 BuildProcessID;

	/**
	 * A map of directory paths to shader jobs contained within that directory.
	 * One entry per XGE task.
	 */
	class FShaderBatch
	{
		TArray<FShaderCommonCompileJobPtr> Jobs;
		bool bTransferFileWritten;

	public:
		const FString& DirectoryBase;
		const FString& InputFileName;
		const FString& SuccessFileName;
		const FString& OutputFileName;

		int32 BatchIndex;
		int32 DirectoryIndex;

		FString WorkingDirectory;
		FString OutputFileNameAndPath;
		FString SuccessFileNameAndPath;
		FString InputFileNameAndPath;
		
		FShaderBatch(const FString& InDirectoryBase, const FString& InInputFileName, const FString& InSuccessFileName, const FString& InOutputFileName, int32 InDirectoryIndex, int32 InBatchIndex)
			: bTransferFileWritten(false)
			, DirectoryBase(InDirectoryBase)
			, InputFileName(InInputFileName)
			, SuccessFileName(InSuccessFileName)
			, OutputFileName(InOutputFileName)
		{
			SetIndices(InDirectoryIndex, InBatchIndex);
		}

		void SetIndices(int32 InDirectoryIndex, int32 InBatchIndex);

		void CleanUpFiles(bool keepInputFile);

		inline int32 NumJobs()
		{
			return Jobs.Num();
		}
		inline const TArray<FShaderCommonCompileJobPtr>& GetJobs() const
		{
			return Jobs;
		}

		void AddJob(FShaderCommonCompileJobPtr Job);
		
		void WriteTransferFile();
	};
	TArray<FShaderBatch*> ShaderBatchesInFlight;
	TArray<FShaderBatch*> ShaderBatchesFull;
	TSparseArray<FShaderBatch*> ShaderBatchesIncomplete;

	/** The full path to the two working directories for XGE shader builds. */
	const FString XGEWorkingDirectory;
	uint32 XGEDirectoryIndex;

	uint64 LastAddTime;
	uint64 StartTime;
	int32 BatchIndexToCreate;
	int32 BatchIndexToFill;

	FDateTime ScriptFileCreationTime;

	void PostCompletedJobsForBatch(FShaderBatch* Batch);

	void GatherResultsFromXGE();

public:
	/** Initialization constructor. */
	FShaderCompileXGEThreadRunnable_XmlInterface(class FShaderCompilingManager* InManager);
	virtual ~FShaderCompileXGEThreadRunnable_XmlInterface();

	/** Main work loop. */
	virtual int32 CompilingLoop() override;

	static bool IsSupported();
};

#endif // PLATFORM_WINDOWS

class FShaderCompileDistributedThreadRunnable_Interface : public FShaderCompileThreadRunnableBase
{
	uint32 NumDispatchedJobs;

	TSparseArray<class FDistributedShaderCompilerTask*> DispatchedTasks;

public:
	/** Initialization constructor. */
	FShaderCompileDistributedThreadRunnable_Interface(class FShaderCompilingManager* InManager, class IDistributedBuildController& InController);
	virtual ~FShaderCompileDistributedThreadRunnable_Interface();

	/** Main work loop. */
	virtual int32 CompilingLoop() override;

	static bool IsSupported();

protected:
	
	IDistributedBuildController& CachedController;
	TMap<EShaderPlatform, TArray<FString> >	PlatformShaderInputFilesCache;

private:

	TArray<FString> GetDependencyFilesForJobs(TArray<FShaderCommonCompileJobPtr>& Jobs);
	void DispatchShaderCompileJobsBatch(TArray<FShaderCommonCompileJobPtr>& JobsToSerialize);
};

class FShaderCompileFASTBuildThreadRunnable : public FShaderCompileThreadRunnableBase
{
private:
	/** The handle referring to the XGE console process, if a build is in progress. */
	FProcHandle BuildProcessHandle;
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;

	/** Process ID of the XGE console, if a build is in progress. */
	uint32 BuildProcessID;

	/**
	* A map of directory paths to shader jobs contained within that directory.
	* One entry per XGE task.
	*/
	class FShaderBatch
	{
		TArray<FShaderCommonCompileJobPtr> Jobs;
		bool bTransferFileWritten;

	public:
		bool bSuccessfullyCompleted;
		const FString& DirectoryBase;
		const FString InputFileName;
		const FString& SuccessFileName;
		const FString OutputFileName;

		int32 BatchIndex;
		int32 DirectoryIndex;

		FString WorkingDirectory;
		FString OutputFileNameAndPath;
		FString SuccessFileNameAndPath;
		FString InputFileNameAndPath;

		FShaderBatch(const FString& InDirectoryBase, const FString& InInputFileName, const FString& InSuccessFileName, const FString& InOutputFileName, int32 InDirectoryIndex, int32 InBatchIndex)
			: bTransferFileWritten(false)
			, bSuccessfullyCompleted(false)
			, DirectoryBase(InDirectoryBase)
			, InputFileName(InInputFileName)
			, SuccessFileName(InSuccessFileName)
			, OutputFileName(InOutputFileName)
		{
			SetIndices(InDirectoryIndex, InBatchIndex);
		}

		void SetIndices(int32 InDirectoryIndex, int32 InBatchIndex);

		void CleanUpFiles(bool keepInputFile);

		inline int32 NumJobs()
		{
			return Jobs.Num();
		}
		inline const TArray<FShaderCommonCompileJobPtr>& GetJobs() const
		{
			return Jobs;
		}

		void AddJob(FShaderCommonCompileJobPtr Job);

		void WriteTransferFile();
	};
	TArray<FShaderBatch*> ShaderBatchesInFlight;
	int32 ShaderBatchesInFlightCompleted;
	TArray<FShaderBatch*> ShaderBatchesFull;
	TSparseArray<FShaderBatch*> ShaderBatchesIncomplete;

	/** The full path to the two working directories for XGE shader builds. */
	const FString FASTBuildWorkingDirectory;
	uint32 FASTBuildDirectoryIndex;

	uint64 LastAddTime;
	uint64 StartTime;
	int32 BatchIndexToCreate;
	int32 BatchIndexToFill;

	FDateTime ScriptFileCreationTime;

	void PostCompletedJobsForBatch(FShaderBatch* Batch);

	void GatherResultsFromFASTBuild();

public:
	/** Initialization constructor. */
	FShaderCompileFASTBuildThreadRunnable(class FShaderCompilingManager* InManager);
	virtual ~FShaderCompileFASTBuildThreadRunnable();

	/** Main work loop. */
	virtual int32 CompilingLoop() override;

	static bool IsSupported();
};

/** Results for a single compiled and finalized shader map. */
using FShaderMapFinalizeResults = FShaderMapCompileResults;

class FShaderCompilerStats
{
public:
	struct FShaderCompilerSinglePermutationStat
	{
		FShaderCompilerSinglePermutationStat(FString PermutationString, uint32 Compiled, uint32 Cooked)
			: PermutationString(PermutationString)
			, Compiled(Compiled)
			, Cooked(Cooked)
			, CompiledDouble(0)
			, CookedDouble(0)

		{}
		FString PermutationString;
		uint32 Compiled;
		uint32 Cooked;
		uint32 CompiledDouble;
		uint32 CookedDouble;
	};
	struct FShaderStats
	{
		TArray<FShaderCompilerSinglePermutationStat> PermutationCompilations;
		uint32 Compiled = 0;
		uint32 Cooked = 0;
		uint32 CompiledDouble = 0;
		uint32 CookedDouble = 0;
		float CompileTime = 0.f;

	};
	using ShaderCompilerStats = TMap<FString, FShaderStats>;


	ENGINE_API void RegisterCookedShaders(uint32 NumCooked, float CompileTime, EShaderPlatform Platform, const FString MaterialPath, FString PermutationString = FString(""));
	ENGINE_API void RegisterCompiledShaders(uint32 NumPermutations, EShaderPlatform Platform, const FString MaterialPath, FString PermutationString = FString(""));
	ENGINE_API const TSparseArray<ShaderCompilerStats>& GetShaderCompilerStats() { return CompileStats; }
	ENGINE_API void WriteStats();

private:
	FCriticalSection CompileStatsLock;
	TSparseArray<ShaderCompilerStats> CompileStats;
};



/**  
 * Manager of asynchronous and parallel shader compilation.
 * This class contains an interface to enqueue and retreive asynchronous shader jobs, and manages a FShaderCompileThreadRunnable.
 */
class FShaderCompilingManager
{
	friend class FShaderCompileThreadRunnableBase;
	friend class FShaderCompileThreadRunnable;

#if PLATFORM_WINDOWS
	friend class FShaderCompileXGEThreadRunnable_XmlInterface;
#endif // PLATFORM_WINDOWS
	friend class FShaderCompileDistributedThreadRunnable_Interface;
	friend class FShaderCompileFASTBuildThreadRunnable;

private:

	//////////////////////////////////////////////////////
	// Thread shared properties: These variables can only be read from or written to when a lock on CompileQueueSection is obtained, since they are used by both threads.

	/** Tracks whether we are compiling while the game is running.  If true, we need to throttle down shader compiling CPU usage to avoid starving the runtime threads. */
	bool bCompilingDuringGame;

	/** Map from shader map Id to the compile results for that map, used to gather compiled results. */
	TMap<int32, FPendingShaderMapCompileResultsPtr> ShaderMapJobs;

	/** Number of jobs currently being compiled.  This includes CompileQueue and any jobs that have been assigned to workers but aren't complete yet. */
	int32 NumExternalJobs;

	void ReleaseJob(FShaderCommonCompileJobPtr& Job);
	void ReleaseJob(FShaderCommonCompileJob* Job);

	/** Critical section used to gain access to the variables above that are shared by both the main thread and the FShaderCompileThreadRunnable. */
	FCriticalSection CompileQueueSection;

	/** Collection of all outstanding jobs */
	FShaderCompileJobCollection AllJobs;

	//////////////////////////////////////////////////////
	// Main thread state - These are only accessed on the main thread and used to track progress

	/** Map from shader map id to results being finalized.  Used to track shader finalizations over multiple frames. */
	TMap<int32, FShaderMapFinalizeResults> PendingFinalizeShaderMaps;

	/** The threads spawned for shader compiling. */
	TArray<TUniquePtr<FShaderCompileThreadRunnableBase>> Threads;

	//////////////////////////////////////////////////////
	// Configuration properties - these are set only on initialization and can be read from either thread

	/** Number of busy threads to use for shader compiling while loading. */
	uint32 NumShaderCompilingThreads;
	/** Number of busy threads to use for shader compiling while in game. */
	uint32 NumShaderCompilingThreadsDuringGame;
	/** Largest number of jobs that can be put in the same batch. */
	int32 MaxShaderJobBatchSize;
	/** Number of runs through single-threaded compiling before we can retry to compile through workers. -1 if not used. */
	int32 NumSingleThreadedRunsBeforeRetry;
	/** Process Id of UE4. */
	uint32 ProcessId;
	/** Whether to allow compiling shaders through the worker application, which allows multiple cores to be used. */
	bool bAllowCompilingThroughWorkers;
	/** Whether to allow shaders to compile in the background or to block after each material. */
	bool bAllowAsynchronousShaderCompiling;
	/** Whether to ask to retry a failed shader compile error. */
	bool bPromptToRetryFailedShaderCompiles;
	/** Whether to log out shader job completion times on the worker thread.  Useful for tracking down which global shader is taking a long time. */
	bool bLogJobCompletionTimes;
	/** Target execution time for ProcessAsyncResults.  Larger values speed up async shader map processing but cause more hitchiness while async compiling is happening. */
	float ProcessGameThreadTargetTime;
	/** Base directory where temporary files are written out during multi core shader compiling. */
	FString ShaderBaseWorkingDirectory;
	/** Absolute version of ShaderBaseWorkingDirectory. */
	FString AbsoluteShaderBaseWorkingDirectory;
	/** Absolute path to the directory to dump shader debug info to. */
	FString AbsoluteShaderDebugInfoDirectory;
	/** Name of the shader worker application. */
	FString ShaderCompileWorkerName;

	/** 
	 * Tracks the total time that shader compile workers have been busy since startup.  
	 * Useful for profiling the shader compile worker thread time.
	 */
	double WorkersBusyTime;

	/** 
	 * Tracks which opt-in shader platforms have their warnings suppressed.
	 */
	uint64 SuppressedShaderPlatforms;

	/** Cached Engine loop initialization state */
	bool bIsEngineLoopInitialized;

	/** Interface to the build distribution controller (XGE/SN-DBS) */
	IDistributedBuildController* BuildDistributionController;

	/** Opt out of material shader compilation and instead place an empty shader map. */
	bool bNoShaderCompilation;

	/** Launches the worker, returns the launched process handle. */
	FProcHandle LaunchWorker(const FString& WorkingDirectory, uint32 ProcessId, uint32 ThreadId, const FString& WorkerInputFile, const FString& WorkerOutputFile);

	/** Blocks on completion of the given shader maps. */
	void BlockOnShaderMapCompletion(const TArray<int32>& ShaderMapIdsToFinishCompiling, TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps);

	/** Blocks on completion of all shader maps. */
	void BlockOnAllShaderMapCompletion(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps);

	/** Adds compiled results to the CompiledShaderMaps, merging with the existing ones as necessary. */
	void AddCompiledResults(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps, int32 ShaderMapIdx, const FShaderMapFinalizeResults& Results);

	/** Finalizes the given shader map results and optionally assigns the affected shader maps to materials, while attempting to stay within an execution time budget. */
	void ProcessCompiledShaderMaps(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps, float TimeBudget);

	/** Finalizes the given Niagara shader map results and assigns the affected shader maps to Niagara scripts, while attempting to stay within an execution time budget. */
	void ProcessCompiledNiagaraShaderMaps(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps, float TimeBudget);

	/** Propagate the completed compile to primitives that might be using the materials compiled. */
	void PropagateMaterialChangesToPrimitives(const TMap<TRefCountPtr<FMaterial>, TRefCountPtr<FMaterialShaderMap>>& MaterialsToUpdate);

	/** Recompiles shader jobs with errors if requested, and returns true if a retry was needed. */
	bool HandlePotentialRetryOnError(TMap<int32, FShaderMapFinalizeResults>& CompletedShaderMaps);
	
	/** Checks if any target platform down't support remote shader compiling */
	bool AllTargetPlatformSupportsRemoteShaderCompiling();
	
	/** Returns the first remote compiler controller found */
	IDistributedBuildController* FindRemoteCompilerController() const;

public:
	
	ENGINE_API FShaderCompilingManager();

	ENGINE_API int32 GetNumPendingJobs() const;
	ENGINE_API int32 GetNumOutstandingJobs() const;

	/** 
	 * Returns whether to display a notification that shader compiling is happening in the background. 
	 * Note: This is dependent on NumOutstandingJobs which is updated from another thread, so the results are non-deterministic.
	 */
	bool ShouldDisplayCompilingNotification() const 
	{ 
		// Heuristic based on the number of jobs outstanding
		return GetNumOutstandingJobs() > 80 || GetNumPendingJobs() > 80 || NumExternalJobs > 10;
	}

	bool AllowAsynchronousShaderCompiling() const 
	{
		return bAllowAsynchronousShaderCompiling;
	}

	/** 
	 * Returns whether async compiling is happening.
	 * Note: This is dependent on NumOutstandingJobs which is updated from another thread, so the results are non-deterministic.
	 */
	bool IsCompiling() const
	{
		return GetNumOutstandingJobs() > 0 || PendingFinalizeShaderMaps.Num() > 0 || GetNumPendingJobs() > 0 || NumExternalJobs > 0;
	}

	/**
	 * return true if we have shader jobs in any state
	 * shader jobs are removed when they are applied to the gamethreadshadermap
	 * accessable from gamethread
	 */
	bool HasShaderJobs() const
	{
		return ShaderMapJobs.Num() > 0 || PendingFinalizeShaderMaps.Num() > 0;
	}

	/** 
	 * Returns the number of outstanding compile jobs.
	 * Note: This is dependent on NumOutstandingJobs which is updated from another thread, so the results are non-deterministic.
	 */
	int32 GetNumRemainingJobs() const
	{
		return GetNumOutstandingJobs() + NumExternalJobs;
	}

	void SetExternalJobs(int32 NumJobs)
	{
		NumExternalJobs = NumJobs;
	}

	enum class EDumpShaderDebugInfo : int32
	{
		Never				= 0,
		Always				= 1,
		OnError				= 2,
		OnErrorOrWarning	= 3
	};

	ENGINE_API EDumpShaderDebugInfo GetDumpShaderDebugInfo() const;
	ENGINE_API FString CreateShaderDebugInfoPath(const FShaderCompilerInput& ShaderCompilerInput) const;
	ENGINE_API bool ShouldRecompileToDumpShaderDebugInfo(const FShaderCompileJob& Job) const;
	ENGINE_API bool ShouldRecompileToDumpShaderDebugInfo(const FShaderCompilerInput& Input, const FShaderCompilerOutput& Output, bool bSucceeded) const;

	const FString& GetAbsoluteShaderDebugInfoDirectory() const
	{
		return AbsoluteShaderDebugInfoDirectory;
	}

	bool AreWarningsSuppressed(const EShaderPlatform Platform) const
	{
		return (SuppressedShaderPlatforms & (static_cast<uint64>(1) << Platform)) != 0;
	}

	void SuppressWarnings(const EShaderPlatform Platform)
	{
		SuppressedShaderPlatforms |= static_cast<uint64>(1) << Platform;
	}

	bool IsShaderCompilationSkipped() const
	{
		return bNoShaderCompilation;
	}

	void SkipShaderCompilation(bool toggle)
	{
		bNoShaderCompilation = toggle;
	}

	/** Prepares a job of the given type for compilation.  If a job with the given Id/Key already exists, it will attempt to adjust to the higher priority if possible, and nullptr will be returned.
	  * If a non-nullptr is returned, the given job should be filled out with relevant information, then passed to SubmitJobs() when ready
	  */
	ENGINE_API FShaderCompileJob* PrepareShaderCompileJob(uint32 Id, const FShaderCompileJobKey& Key, EShaderCompileJobPriority Priority);
	ENGINE_API FShaderPipelineCompileJob* PreparePipelineCompileJob(uint32 Id, const FShaderPipelineCompileJobKey& Key, EShaderCompileJobPriority Priority);

	/** This is an entry point for all jobs that have finished the compilation. Can be called from multiple threads.*/
	ENGINE_API void ProcessFinishedJob(FShaderCommonCompileJob* FinishedJob);

	/** 
	 * Adds shader jobs to be asynchronously compiled. 
	 * FinishCompilation or ProcessAsyncResults must be used to get the results.
	 */
	ENGINE_API void SubmitJobs(TArray<FShaderCommonCompileJobPtr>& NewJobs, const FString MaterialBasePath, FString PermutationString = FString(""));

	/**
	* Removes all outstanding compile jobs for the passed shader maps.
	*/
	ENGINE_API void CancelCompilation(const TCHAR* MaterialName, const TArray<int32>& ShaderMapIdsToCancel);

	/** 
	 * Blocks until completion of the requested shader maps.  
	 * This will not assign the shader map to any materials, the caller is responsible for that.
	 */
	ENGINE_API void FinishCompilation(const TCHAR* MaterialName, const TArray<int32>& ShaderMapIdsToFinishCompiling);

	/** 
	 * Blocks until completion of all async shader compiling, and assigns shader maps to relevant materials.
	 * This should be called before exit if the DDC needs to be made up to date. 
	 */
	ENGINE_API void FinishAllCompilation();

	/** 
	 * Shutdown the shader compiler manager, this will shutdown immediately and not process any more shader compile requests. 
	 */
	ENGINE_API void Shutdown();


	/** 
	 * Processes completed asynchronous shader maps, and assigns them to relevant materials.
	 * @param bLimitExecutionTime - When enabled, ProcessAsyncResults will be bandwidth throttled by ProcessGameThreadTargetTime, to limit hitching.
	 *		ProcessAsyncResults will then have to be called often to finish all shader maps (eg from Tick).  Otherwise, all compiled shader maps will be processed.
	 * @param bBlockOnGlobalShaderCompletion - When enabled, ProcessAsyncResults will block until global shader maps are complete.
	 *		This must be done before using global shaders for rendering.
	 */
	ENGINE_API void ProcessAsyncResults(bool bLimitExecutionTime, bool bBlockOnGlobalShaderCompletion);

	/**
	 * Returns true if the given shader compile worker is still running.
	 */
	static bool IsShaderCompilerWorkerRunning(FProcHandle & WorkerHandle);
};

/** The global shader compiling thread manager. */
extern ENGINE_API FShaderCompilingManager* GShaderCompilingManager;

/** The global shader compiling stats */
extern ENGINE_API FShaderCompilerStats* GShaderCompilerStats;

/** The shader precompilers for each platform.  These are only set during the console shader compilation while cooking or in the PrecompileShaders commandlet. */
extern class FConsoleShaderPrecompiler* GConsoleShaderPrecompilers[SP_NumPlatforms];

/** Enqueues a shader compile job with GShaderCompilingManager. */
extern ENGINE_API void GlobalBeginCompileShader(
	const FString& DebugGroupName,
	const class FVertexFactoryType* VFType,
	const class FShaderType* ShaderType,
	const class FShaderPipelineType* ShaderPipelineType,
	int32 PermutationId,
	const TCHAR* SourceFilename,
	const TCHAR* FunctionName,
	FShaderTarget Target,
	FShaderCompilerInput& Input,
	bool bAllowDevelopmentShaderCompile = true,
	const FString& DebugDescription = "",
	const FString& DebugExtension = ""
	);

extern void GetOutdatedShaderTypes(TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes);

/** Implementation of the 'recompileshaders' console command.  Recompiles shaders at runtime based on various criteria. */
extern bool RecompileShaders(const TCHAR* Cmd, FOutputDevice& Ar);

/** Returns whether all global shader types containing the substring are complete and ready for rendering. if type name is null, check everything */
extern ENGINE_API bool IsGlobalShaderMapComplete(const TCHAR* TypeNameSubstring = nullptr);

/** Returns the delegate triggered when global shaders compilation jobs start. */
DECLARE_MULTICAST_DELEGATE(FOnGlobalShadersCompilation);
extern ENGINE_API FOnGlobalShadersCompilation& GetOnGlobalShaderCompilation();

/**
* Makes sure all global shaders are loaded and/or compiled for the passed in platform.
* Note: if compilation is needed, this only kicks off the compile.
*
* @param	Platform						Platform to verify global shaders for
* @param	bLoadedFromCacheFile			Load the shaders from cache, will error out and not compile shaders if missing
* @param	OutdatedShaderTypes				Optional list of shader types, will trigger compilation job for shader types found in this list even if the map already has the shader.
* @param	OutdatedShaderPipelineTypes		Optional list of shader pipeline types, will trigger compilation job for shader pipeline types found in this list even if the map already has the pipeline.
*/
extern ENGINE_API void VerifyGlobalShaders(EShaderPlatform Platform, bool bLoadedFromCacheFile, const TArray<const FShaderType*>* OutdatedShaderTypes = nullptr, const TArray<const FShaderPipelineType*>* OutdatedShaderPipelineTypes = nullptr);
extern ENGINE_API void VerifyGlobalShaders(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, bool bLoadedFromCacheFile, const TArray<const FShaderType*>* OutdatedShaderTypes = nullptr, const TArray<const FShaderPipelineType*>* OutdatedShaderPipelineTypes = nullptr);

/**
* Forces a recompile of the global shaders.
*/
extern ENGINE_API void RecompileGlobalShaders();

/**
* Recompiles global shaders and material shaders
* rebuilds global shaders and also
* clears the cooked platform data for all materials if there is a global shader change detected
* can be slow
*/
extern ENGINE_API bool RecompileChangedShadersForPlatform(const FString& PlatformName);

/**
* Begins recompiling the specified global shader types, and flushes their bound shader states.
* FinishRecompileGlobalShaders must be called after this and before using the global shaders for anything.
*/
extern ENGINE_API void BeginRecompileGlobalShaders(const TArray<const FShaderType*>& OutdatedShaderTypes, const TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, EShaderPlatform ShaderPlatform, const ITargetPlatform* TargetPlatform = nullptr);

/** Finishes recompiling global shaders.  Must be called after BeginRecompileGlobalShaders. */
extern ENGINE_API void FinishRecompileGlobalShaders();

/** Called by the shader compiler to process completed global shader jobs. */
extern ENGINE_API void ProcessCompiledGlobalShaders(const TArray<FShaderCommonCompileJobPtr>& CompilationResults);

/**
* Saves the global shader map as a file for the target platform.
* @return the name of the file written
*/
extern ENGINE_API FString SaveGlobalShaderFile(EShaderPlatform Platform, FString SavePath, class ITargetPlatform* TargetPlatform = nullptr);

struct FODSCRequestPayload
{
	/** The shader platform to compile for. */
	EShaderPlatform ShaderPlatform;

	/** Which material do we compile for?. */
	FString MaterialName;

	/** The vertex factory type name to compile shaders for. */
	FString VertexFactoryName;

	/** The name of the pipeline to compile shaders for. */
	FString PipelineName;

	/** An array of shader type names for each stage in the Pipeline. */
	TArray<FString> ShaderTypeNames;

	/** A hash of the above information to uniquely identify a Request. */
	FString RequestHash;

	ENGINE_API FODSCRequestPayload() {};
	ENGINE_API FODSCRequestPayload(EShaderPlatform InShaderPlatform, const FString& InMaterialName, const FString& InVertexFactoryName, const FString& InPipelineName, const TArray<FString>& InShaderTypeNames, const FString& InRequestHash);

	/**
	* Serializes FODSCRequestPayload value from or into this archive.
	*
	* @param Ar The archive to serialize to.
	* @param Value The value to serialize.
	* @return The archive.
	*/
	ENGINE_API friend FArchive& operator<<(FArchive& Ar, FODSCRequestPayload& Elem);
};

/**
* Recompiles global shaders
*
* @param PlatformName					Name of the Platform the shaders are compiled for
* @param OutputDirectory				The directory the compiled data will be stored to
* @param MaterialsToLoad				List of Materials that need to be loaded and compiled
* @param MeshMaterialMaps				Mesh material maps
* @param ModifiedFiles					Returns the list of modified files if not NULL
* @param bCompileChangedShaders		Whether to compile all changed shaders or the specific material that is passed
**/
extern ENGINE_API void RecompileShadersForRemote(
	const FString& PlatformName,
	EShaderPlatform ShaderPlatform,
	const FString& OutputDirectory,
	const TArray<FString>& MaterialsToLoad,
	const TArray<FODSCRequestPayload>& ShadersToRecompile,
	TArray<uint8>* MeshMaterialMaps,
	TArray<FString>* ModifiedFiles,
	bool bCompileChangedShaders = true);

extern ENGINE_API void CompileGlobalShaderMap(bool bRefreshShaderMap=false);
extern ENGINE_API void CompileGlobalShaderMap(ERHIFeatureLevel::Type InFeatureLevel, bool bRefreshShaderMap=false);
extern ENGINE_API void CompileGlobalShaderMap(EShaderPlatform Platform, bool bRefreshShaderMap = false);
extern ENGINE_API void CompileGlobalShaderMap(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, bool bRefreshShaderMap);

extern ENGINE_API FString GetGlobalShaderMapDDCKey();

extern ENGINE_API FString GetMaterialShaderMapDDCKey();

/**
* Handles serializing in MeshMaterialMaps from a CookOnTheFly command and applying them to the in-memory shadermaps.
*
* @param MeshMaterialMaps				Byte array that contains the serialized shadermap from across the network.
* @param MaterialsToLoad				The materials contained in the MeshMaterialMaps
**/
extern ENGINE_API void ProcessCookOnTheFlyShaders(bool bReloadGlobalShaders, const TArray<uint8>& MeshMaterialMaps, const TArray<FString>& MaterialsToLoad);
