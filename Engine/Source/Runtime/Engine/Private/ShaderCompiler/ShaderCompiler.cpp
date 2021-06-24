// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompiler.cpp: Platform independent shader compilations.
=============================================================================*/

#include "ShaderCompiler.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "HAL/ExceptionHandling.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/Guid.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryHasher.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInterface.h"
#include "StaticBoundShaderState.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Misc/CoreDelegates.h"
#include "EditorSupportDelegates.h"
#include "GlobalShader.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Components/PrimitiveComponent.h"
#include "DerivedDataCacheInterface.h"
#include "ShaderDerivedDataVersion.h"
#include "Misc/FileHelper.h"
#include "StaticBoundShaderState.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "ProfilingDebugging/CookStats.h"
#include "SceneInterface.h"
#include "ShaderCodeLibrary.h"
#include "MeshMaterialShaderType.h"
#include "ShaderParameterMetadata.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "ShaderCore.h"
#include "DistributedBuildInterface/Public/DistributedBuildControllerInterface.h"
#include "Misc/ScopeRWLock.h"
#include "Async/ParallelFor.h"
#include "RenderUtils.h"
#if WITH_EDITOR
#include "Rendering/StaticLightingSystemInterface.h"
#endif
#if WITH_ODSC
#include "ODSC/ODSCManager.h"
#endif
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "ShaderCompiler"

DEFINE_LOG_CATEGORY(LogShaderCompilers);

// Switch to Verbose after initial testing
#define UE_SHADERCACHE_LOG_LEVEL		Verbose

int32 GShaderCompilerJobCache = 1;
static FAutoConsoleVariableRef CVarShaderCompilerJobCache(
	TEXT("r.ShaderCompiler.JobCache"),
	GShaderCompilerJobCache,
	TEXT("if != 0, shader compiler cache (based on the unpreprocessed input hash) will be disabled. By default, it is enabled."),
	ECVF_Default
);

int32 GShaderCompilerMaxJobCacheMemoryMB = 16LL * 1024LL;
static FAutoConsoleVariableRef CVarShaderCompilerMaxJobCacheMemoryMB(
	TEXT("r.ShaderCompiler.MaxJobCacheMemoryMB"),
	GShaderCompilerMaxJobCacheMemoryMB,
	TEXT("if != 0, shader compiler cache will be limited to this many megabytes (16GB by default). If 0, the usage will be unlimited. Minimum of this or r.ShaderCompiler.MaxJobCacheMemoryPercent applies."),
	ECVF_Default
);

int32 GShaderCompilerMaxJobCacheMemoryPercent = 5;
static FAutoConsoleVariableRef CVarShaderCompilerMaxJobCacheMemoryPercent(
	TEXT("r.ShaderCompiler.MaxJobCacheMemoryPercent"),
	GShaderCompilerMaxJobCacheMemoryPercent,
	TEXT("if != 0, shader compiler cache will be limited to this percentage of available physical RAM (5% by default). If 0, the usage will be unlimited. Minimum of this or r.ShaderCompiler.MaxJobCacheMemoryMB applies."),
	ECVF_Default
);

int32 GShaderCompilerDumpCompileJobInputs = 0;
static FAutoConsoleVariableRef CVarShaderCompilerDumpCompileJobInputs(
	TEXT("r.ShaderCompiler.DumpCompileJobInputs"),
	GShaderCompilerDumpCompileJobInputs,
	TEXT("if != 0, unpreprocessed input of the shader compiler jobs will be dumped into the debug directory for closer inspection. This is a debugging feature which is disabled by default."),
	ECVF_Default
);

int32 GShaderCompilerCacheStatsPrintoutInterval = 180;
static FAutoConsoleVariableRef CVarShaderCompilerCacheStatsPrintoutInterval(
	TEXT("r.ShaderCompiler.CacheStatsPrintoutInterval"),
	GShaderCompilerCacheStatsPrintoutInterval,
	TEXT("Minimum interval (in seconds) between printing out debugging stats (by default, no closer than each 3 minutes)."),
	ECVF_Default
);


#if ENABLE_COOK_STATS
namespace GlobalShaderCookStats
{
	FCookStats::FDDCResourceUsageStats UsageStats;
	static int32 ShadersCompiled = 0;

	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("GlobalShader.Usage"), TEXT(""));
		AddStat(TEXT("GlobalShader.Misc"), FCookStatsManager::CreateKeyValueArray(
			TEXT("ShadersCompiled"), ShadersCompiled
		));
	});
}
#endif

FString GetGlobalShaderMapDDCKey()
{
	return FString(GLOBALSHADERMAP_DERIVEDDATA_VER);
}

FString GetMaterialShaderMapDDCKey()
{
	return FString(MATERIALSHADERMAP_DERIVEDDATA_VER);
}

namespace ShaderCompiler
{
	bool IsJobCacheEnabled()
	{
		return GShaderCompilerJobCache != 0;
	}
}

// The Id of 0 is reserved for global shaders
FThreadSafeCounter FShaderCommonCompileJob::JobIdCounter(2);

uint32 FShaderCommonCompileJob::GetNextJobId()
{
	uint32 Id = JobIdCounter.Increment();
	if (Id == UINT_MAX)
	{
		JobIdCounter.Set(2);
	}
	return Id;
}

FShaderPipelineCompileJob::FShaderPipelineCompileJob(uint32 InHash, uint32 InId, EShaderCompileJobPriority InPriroity, const FShaderPipelineCompileJobKey& InKey) :
	FShaderCommonCompileJob(Type, InHash, InId, InPriroity),
	Key(InKey)
{
	const auto& Stages = InKey.ShaderPipeline->GetStages();
	StageJobs.Empty(Stages.Num());
	for (const FShaderType* ShaderType : Stages)
	{
		const FShaderCompileJobKey StageKey(ShaderType, InKey.VFType, InKey.PermutationId);
		StageJobs.Add(new FShaderCompileJob(StageKey.MakeHash(InId), InId, InPriroity, StageKey));
	}
}

FShaderCompileJobCollection::FShaderCompileJobCollection()
{
	FMemory::Memzero(PendingJobs);
	FMemory::Memzero(NumPendingJobs);

	LogJobsCacheStatsCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("r.ShaderCompiler.LogCacheStats"),
		TEXT("Prints out the stats for the in-memory shader job cache."),
		FConsoleCommandDelegate::CreateRaw(this, &FShaderCompileJobCollection::HandleLogJobsCacheStats),
		ECVF_Default
	);
}

void FShaderCompileJobCollection::InternalAddJob(FShaderCommonCompileJob* InJob)
{
	const int32 TypeIndex = (int32)InJob->Type;

	int32 JobIndex = INDEX_NONE;
	if (FreeIndices[TypeIndex].Num() > 0)
	{
		JobIndex = FreeIndices[TypeIndex].Pop(false);
		check(!Jobs[TypeIndex][JobIndex].IsValid());
		Jobs[TypeIndex][JobIndex] = InJob;
	}
	else
	{
		JobIndex = Jobs[TypeIndex].Add(InJob);
	}

	check(Jobs[TypeIndex][JobIndex].IsValid());
	JobHash[TypeIndex].Add(InJob->Hash, JobIndex);
	
	check(InJob->Priority != EShaderCompileJobPriority::None);
	check(InJob->PendingPriority == EShaderCompileJobPriority::None);
	check(InJob->JobIndex == INDEX_NONE);
	InJob->JobIndex = JobIndex;
}

static FShaderCommonCompileJob* CloneJob_Single(const FShaderCompileJob* SrcJob)
{
	FShaderCompileJob* Job = new FShaderCompileJob(SrcJob->Hash, SrcJob->Id, SrcJob->Priority, SrcJob->Key);
	Job->PendingShaderMap = SrcJob->PendingShaderMap;
	Job->Input = SrcJob->Input;
	if (SrcJob->bInputHashSet)
	{
		Job->InputHash = SrcJob->InputHash;
		Job->bInputHashSet = true;
	}
	ensure(Job->bInputHashSet == SrcJob->bInputHashSet);
	return Job;
}

static FShaderCommonCompileJob* CloneJob_Pipeline(const FShaderPipelineCompileJob* SrcJob)
{
	FShaderPipelineCompileJob* Job = new FShaderPipelineCompileJob(SrcJob->Hash, SrcJob->Id, SrcJob->Priority, SrcJob->Key);
	check(Job->StageJobs.Num() == SrcJob->StageJobs.Num());
	Job->PendingShaderMap = SrcJob->PendingShaderMap;

	for(int32 i = 0; i < SrcJob->StageJobs.Num(); ++i)
	{
		Job->StageJobs[i]->Input = SrcJob->StageJobs[i]->Input;
	}

	if (SrcJob->bInputHashSet)
	{
		Job->InputHash = SrcJob->InputHash;
		Job->bInputHashSet = true;
	}
	ensure(Job->bInputHashSet == SrcJob->bInputHashSet);
	return Job;
}

static FShaderCommonCompileJob* CloneJob(const FShaderCommonCompileJob* SrcJob)
{
	switch (SrcJob->Type)
	{
	case EShaderCompileJobType::Single: return CloneJob_Single(static_cast<const FShaderCompileJob*>(SrcJob));
	case EShaderCompileJobType::Pipeline:  return CloneJob_Pipeline(static_cast<const FShaderPipelineCompileJob*>(SrcJob));
	default: checkNoEntry(); return nullptr;
	}
}

void FShaderCompileJobCollection::InternalSetPriority(FShaderCommonCompileJob* Job, EShaderCompileJobPriority InPriority)
{
	const int32 PriorityIndex = (int32)InPriority;

	if (Job->PendingPriority != EShaderCompileJobPriority::None)
	{
		// Job hasn't started yet, move it to the pending list for the new priority
		const int32 PrevPriorityIndex = (int32)Job->PendingPriority;
		check(Job->PendingPriority == Job->Priority);
		check(NumPendingJobs[PrevPriorityIndex] > 0);
		NumPendingJobs[PrevPriorityIndex]--;
		Job->Unlink();

		NumPendingJobs[PriorityIndex]++;
		ensure(!ShaderCompiler::IsJobCacheEnabled() || Job->bInputHashSet);
		Job->LinkHead(PendingJobs[PriorityIndex]);
		Job->Priority = InPriority;
		Job->PendingPriority = InPriority;
	}
	else if (!Job->bFinalized &&
		Job->CurrentWorker == EShaderCompilerWorkerType::XGE &&
		InPriority == EShaderCompileJobPriority::ForceLocal)
	{
		FShaderCommonCompileJob* NewJob = CloneJob(Job);
		NewJob->Priority = InPriority;
		const int32 NewNumPendingJobs = NewJob->PendingShaderMap->NumPendingJobs.Increment();
		checkf(NewNumPendingJobs > 1, TEXT("Invalid number of pending jobs %d, should have had at least 1 job previously"), NewNumPendingJobs);
		InternalAddJob(NewJob);

		NumPendingJobs[PriorityIndex]++;
		ensureMsgf(NewJob->bInputHashSet == Job->bInputHashSet, TEXT("Cloned and original jobs should either both have input hash, or both not have it. Job->bInputHashSet=%d, NewJob->bInputHashSet=%d"),
			Job->bInputHashSet,
			NewJob->bInputHashSet
			);
		ensureMsgf(!ShaderCompiler::IsJobCacheEnabled() || NewJob->GetInputHash() == Job->GetInputHash(),
			TEXT("If shader jobs cache is enabled, cloned job should have the same input hash as the original, and it doesn't.")
			);
		NewJob->LinkHead(PendingJobs[PriorityIndex]);
		NewJob->PendingPriority = InPriority;
		NumOutstandingJobs.Increment();

		//UE_LOG(LogShaderCompilers, Display, TEXT("Submitted duplicate 'ForceLocal' shader compile job to replace existing XGE job"));
	}
}

void FShaderCompileJobCollection::InternalRemoveJob(FShaderCommonCompileJob* InJob)
{
	const int32 TypeIndex = (int32)InJob->Type;
	const int32 JobIndex = InJob->JobIndex;

	check(JobIndex != INDEX_NONE);
	check(Jobs[TypeIndex][JobIndex] == InJob);
	check(InJob->PendingPriority == EShaderCompileJobPriority::None);
	InJob->JobIndex = INDEX_NONE;

	JobHash[TypeIndex].Remove(InJob->Hash, JobIndex);
	FreeIndices[TypeIndex].Add(JobIndex);
	Jobs[TypeIndex][JobIndex].SafeRelease();
}


void FShaderCompileJobCollection::RemoveJob(FShaderCommonCompileJob* InJob)
{
	FWriteScopeLock Locker(Lock);
	InternalRemoveJob(InJob);
}

int32 FShaderCompileJobCollection::RemoveAllPendingJobsWithId(uint32 InId)
{
	int32 NumRemoved = 0;
	{
		FWriteScopeLock Locker(Lock);
		for (int32 PriorityIndex = 0; PriorityIndex < NumShaderCompileJobPriorities; ++PriorityIndex)
		{
			for (FShaderCommonCompileJob::TIterator It(PendingJobs[PriorityIndex]); It;)
			{
				FShaderCommonCompileJob& Job = *It;
				It.Next();

				if (Job.Id == InId)
				{
					if (ShaderCompiler::IsJobCacheEnabled())
					{
						JobsInFlight.Remove(Job.GetInputHash());
					}

					check(NumPendingJobs[PriorityIndex] > 0);
					NumPendingJobs[PriorityIndex]--;
					Job.Unlink();
					Job.PendingPriority = EShaderCompileJobPriority::None;
					InternalRemoveJob(&Job);
					++NumRemoved;
				}
			}
		}

		if (ShaderCompiler::IsJobCacheEnabled())
		{
			// Also look into the jobs that are cached
			// Since each entry in DuplicateJobsWaitList is a list, and the head node can be removed, we essentially have to rebuild it
			for (TMap<FSHAHash, FShaderCommonCompileJob*>::TIterator Iter(DuplicateJobsWaitList); Iter; ++Iter)
			{
				FShaderCommonCompileJob* ListHead = Iter.Value();
				FShaderCommonCompileJob* NewListHead = ListHead;

				// each entry in DJWL is a linked list of jobs that share the same ihash
				for (FShaderCommonCompileJob::TIterator It(ListHead); It;)
				{
					FShaderCommonCompileJob& Job = *It;
					It.Next();

					if (Job.Id == InId)
					{
						// if we're removing the list head, we need to update the next
						if (NewListHead == &Job)
						{
							NewListHead = Job.Next();
						}

						Job.Unlink();
						Job.PendingPriority = EShaderCompileJobPriority::None;
						InternalRemoveJob(&Job);
						++NumRemoved;
					}
				}

				if (NewListHead == nullptr)
				{
					// we removed the last job for this hash
					Iter.RemoveCurrent();
				}
				else if (NewListHead != ListHead)
				{
					// update the mapping
					Iter.Value() = NewListHead;
				}
			}
		}
	}

	InternalSubtractNumOutstandingJobs(NumRemoved);

	return NumRemoved;
}

void FShaderCompileJobCollection::SubmitJobs(const TArray<FShaderCommonCompileJobPtr>& InJobs)
{
	if (InJobs.Num() > 0)
	{
		// all jobs (not just actually submitted ones) count as outstanding. This needs to be done early because
		// we may fulfill some of the jobs from the cache (and we will be subtracting them)
		NumOutstandingJobs.Add(InJobs.Num());

		int32 SubmittedJobsCount = 0;
		int32 NumSubmittedJobs[NumShaderCompileJobPriorities] = { 0 };
		{
			// Just precompute the InputHash for each job in multiple-thread.
			if (ShaderCompiler::IsJobCacheEnabled())
			{
				ParallelFor(InJobs.Num(), [&InJobs](int32 Index) { InJobs[Index]->GetInputHash(); });
			}

			FWriteScopeLock Locker(Lock);

			for (FShaderCommonCompileJob* Job : InJobs)
			{
				check(Job->JobIndex != INDEX_NONE);
				check(Job->Priority != EShaderCompileJobPriority::None);
				check(Job->PendingPriority == EShaderCompileJobPriority::None);

				const int32 PriorityIndex = (int32)Job->Priority;
				bool bNewJob = true;
				if (ShaderCompiler::IsJobCacheEnabled())
				{
					const FSHAHash& InputHash = Job->GetInputHash();

					// see if we can find the job in the cache first
					if (TArray<uint8>* ExistingOutput = CompletedJobsCache.Find(InputHash))
					{
						UE_LOG(LogShaderCompilers, UE_SHADERCACHE_LOG_LEVEL, TEXT("There is already a cached job with the ihash %s, processing the new one immediately."), *InputHash.ToString());
						FMemoryReader MemReader(*ExistingOutput);
						Job->SerializeOutput(MemReader);

						// finish the job instantly
						ProcessFinishedJob(Job, true);

						continue;
					}
					// see if another job with the same input hash is being worked on
					else if (FShaderCommonCompileJob** DuplicateInFlight = JobsInFlight.Find(InputHash))
					{
						UE_LOG(LogShaderCompilers, UE_SHADERCACHE_LOG_LEVEL, TEXT("There is an outstanding job with the ihash %s, not submitting another one (adding to wait list)."), *InputHash.ToString());

						// because of the cloned jobs, we need to maintain a separate mapping
						FShaderCommonCompileJob** WaitListHead = DuplicateJobsWaitList.Find(InputHash);
						if (WaitListHead)
						{
							Job->LinkAfter(*WaitListHead);
						}
						else
						{
							DuplicateJobsWaitList.Add(InputHash, Job);
						}
						bNewJob = false;
					}
					else
					{
						// track new jobs so we can dedupe them
						JobsInFlight.Add(InputHash, Job);
					}
				}

				// new job
				if (bNewJob)
				{
					ensure(!ShaderCompiler::IsJobCacheEnabled() || Job->bInputHashSet);
					Job->LinkHead(PendingJobs[PriorityIndex]);

					NumPendingJobs[PriorityIndex]++;
					NumSubmittedJobs[PriorityIndex]++;
					Job->PendingPriority = Job->Priority;
					++SubmittedJobsCount;
				}
			}
		}

		UE_LOG(LogShaderCompilers, UE_SHADERCACHE_LOG_LEVEL, TEXT("Actual jobs submitted %d (of %d new), total outstanding jobs: %d."), SubmittedJobsCount, InJobs.Num(), NumOutstandingJobs.GetValue());

		for (int32 PriorityIndex = 0; PriorityIndex < NumShaderCompileJobPriorities; ++PriorityIndex)
		{
			if (NumSubmittedJobs[PriorityIndex] > 0)
			{
				UE_LOG(LogShaderCompilers, Display, TEXT("Submitted %d shader compile jobs with '%s' priority"),
					NumSubmittedJobs[PriorityIndex],
					ShaderCompileJobPriorityToString((EShaderCompileJobPriority)PriorityIndex));
			}
		}
	}
}

void FShaderCompileJobCollection::HandleLogJobsCacheStats()
{
	LogCachingStats(true);
}

void FShaderCompileJobCollection::ProcessFinishedJob(FShaderCommonCompileJob* FinishedJob, bool bWasCached)
{
	// TODO: have a pending shader map critical section? not clear at this point if we can be accessing the results on another thread at the same time
	FShaderMapCompileResults& ShaderMapResults = *(FinishedJob->PendingShaderMap);
	ShaderMapResults.FinishedJobs.Add(FinishedJob);
	ShaderMapResults.bAllJobsSucceeded = ShaderMapResults.bAllJobsSucceeded && FinishedJob->bSucceeded;

	const int32 NumPendingJobsForSM = ShaderMapResults.NumPendingJobs.Decrement();
	checkf(NumPendingJobsForSM >= 0, TEXT("Problem tracking pending jobs for a SM (%d), number of pending jobs (%d) is negative!"), FinishedJob->Id, NumPendingJobsForSM);

	InternalSubtractNumOutstandingJobs(1);
	if (!bWasCached && ShaderCompiler::IsJobCacheEnabled())
	{
		AddToCacheAndProcessPending(FinishedJob);
	}
}

void FShaderCompileJobCollection::AddToCacheAndProcessPending(FShaderCommonCompileJob* FinishedJob)
{
	if (!ShaderCompiler::IsJobCacheEnabled())
	{
		return;
	}

	ensureMsgf(FinishedJob->bInputHashSet, TEXT("Finished job didn't have input hash set, was shader compiler jobs cache toggled runtime?"));

	const FSHAHash& InputHash = FinishedJob->GetInputHash();
	TArray<uint8> Output;
	FMemoryWriter Writer(Output);
	FinishedJob->SerializeOutput(Writer);

	// TODO: reduce the scope - e.g. SerializeOutput and processing finished jobs can be moved out of it
	FWriteScopeLock JobLocker(Lock);

	// see if there are outstanding jobs that also need to be resolved
	int32 NumOutstandingJobsWithSameHash = 0;
	if (FShaderCommonCompileJob** WaitList = DuplicateJobsWaitList.Find(InputHash))
	{
		FShaderCommonCompileJob* CurHead = *WaitList;
		while (CurHead)
		{
			checkf(CurHead != FinishedJob, TEXT("Job that is being added to cache was also on a waiting list! Error in bookkeeping."));

			FMemoryReader MemReader(Output);
			CurHead->SerializeOutput(MemReader);
			checkf(CurHead->bSucceeded == FinishedJob->bSucceeded, TEXT("Different success status for the job with the same ihash"));

			// finish the job instantly
			ProcessFinishedJob(CurHead, true);
			++NumOutstandingJobsWithSameHash;

			CurHead = CurHead->Next();
		}

		// remove the waitlist head
		DuplicateJobsWaitList.Remove(InputHash);

		if (NumOutstandingJobsWithSameHash > 0)
		{
			UE_LOG(LogShaderCompilers, UE_SHADERCACHE_LOG_LEVEL, TEXT("Processed %d outstanding jobs with the same ihash %s."), NumOutstandingJobsWithSameHash, *InputHash.ToString());
		}
	}

	if (FinishedJob->bSucceeded)
	{
		// we only cache jobs that succeded
		CompletedJobsCache.Add(InputHash, Output, NumOutstandingJobsWithSameHash);
	}

	// remove ourselves from the jobs in flight, if we were there (if this job is a cloned job it might not have been)
	JobsInFlight.Remove(InputHash);
}

void FShaderCompileJobCollection::LogCachingStats(bool bForceLogIgnoringTimeInverval)
{
	static double LastTimeStatsPrinted = FPlatformTime::Seconds();
	if (!bForceLogIgnoringTimeInverval && GShaderCompilerCacheStatsPrintoutInterval > 0 && FPlatformTime::Seconds() - LastTimeStatsPrinted < GShaderCompilerCacheStatsPrintoutInterval)
	{
		return;
	}

	FWriteScopeLock Locker(Lock);	// write lock because logging actually changes the cache state (in a minor way - updating the memory used - but still).
	CompletedJobsCache.LogStats();
	LastTimeStatsPrinted = FPlatformTime::Seconds();
}

int32 FShaderCompileJobCollection::GetNumPendingJobs() const
{
	FReadScopeLock Locker(Lock);
	int32 NumJobs = 0;
	for (int32 i = 0; i < NumShaderCompileJobPriorities; ++i)
	{
		NumJobs += NumPendingJobs[i];
	}
	return NumJobs;
}

int32 FShaderCompileJobCollection::GetPendingJobs(EShaderCompilerWorkerType InWorkerType, EShaderCompileJobPriority InPriority, int32 MinNumJobs, int32 MaxNumJobs, TArray<FShaderCommonCompileJobPtr>& OutJobs)
{
	check(InWorkerType != EShaderCompilerWorkerType::None);
	check(InPriority != EShaderCompileJobPriority::None);

	const int32 PriorityIndex = (int32)InPriority;
	int32 NumPendingJobsOfPriority = 0;
	{
		FReadScopeLock Locker(Lock);
		NumPendingJobsOfPriority = NumPendingJobs[PriorityIndex];
	}

	if (NumPendingJobsOfPriority < MinNumJobs)
	{
		// Not enough jobs
		return 0;
	}

	OutJobs.Reserve(OutJobs.Num() + FMath::Min(MaxNumJobs, NumPendingJobsOfPriority));
	int32 NumJobs = 0;
	{
		FWriteScopeLock Locker(Lock);
		NumJobs = FMath::Min(MaxNumJobs, NumPendingJobs[PriorityIndex]);
		FShaderCommonCompileJob::TIterator It(PendingJobs[PriorityIndex]);
		for (int32 i = 0; i < NumJobs; ++i)
		{
			FShaderCommonCompileJob& Job = *It;
			check(Job.CurrentWorker == EShaderCompilerWorkerType::None);
			check(Job.PendingPriority == InPriority);
			ensure(!ShaderCompiler::IsJobCacheEnabled() || Job.bInputHashSet);

			It.Next();
			Job.Unlink();

			Job.PendingPriority = EShaderCompileJobPriority::None;
			Job.CurrentWorker = InWorkerType;
			OutJobs.Add(&Job);
		}

		NumPendingJobs[PriorityIndex] -= NumJobs;
	}
	return NumJobs;
}

FShaderCompileJob* FShaderCompileJobCollection::PrepareJob(uint32 InId, const FShaderCompileJobKey& InKey, EShaderCompileJobPriority InPriority)
{
	return InternalPrepareJob<FShaderCompileJob>(InId, InKey, InPriority);
}

FShaderPipelineCompileJob* FShaderCompileJobCollection::PrepareJob(uint32 InId, const FShaderPipelineCompileJobKey& InKey, EShaderCompileJobPriority InPriority)
{
	return InternalPrepareJob<FShaderPipelineCompileJob>(InId, InKey, InPriority);
}

static float GRegularWorkerTimeToLive = 20.0f;
static float GBuildWorkerTimeToLive = 600.0f;

// Configuration to retry shader compile through wrokers after a worker has been abandoned
static constexpr int32 GSingleThreadedRunsIdle = -1;
static constexpr int32 GSingleThreadedRunsDisabled = -2;
static constexpr int32 GSingleThreadedRunsIncreaseFactor = 8;
static constexpr int32 GSingleThreadedRunsMaxCount = (1 << 24);

static void ModalErrorOrLog(const FString& Text, int64 CurrentFilePos = 0, int64 ExpectedFileSize = 0)
{
	FString BadFile;
	if (CurrentFilePos > ExpectedFileSize)
{
		// Corrupt file
		BadFile = FString::Printf(TEXT("(Truncated or corrupt output file! Current file pos %lld, file size %lld)"), CurrentFilePos, ExpectedFileSize);
	}

	if (FPlatformProperties::SupportsWindowedMode())
	{
		UE_LOG(LogShaderCompilers, Error, TEXT("%s%s"), *Text, *BadFile);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Text));
		FPlatformMisc::RequestExit(false);
		return;
	}
	else
	{
		UE_LOG(LogShaderCompilers, Fatal, TEXT("%s%s"), *Text, *BadFile);
	}
}

template<class EnumType>
constexpr auto& CastEnumToUnderlyingTypeReference(EnumType& Type)
{
	static_assert(TIsEnum<EnumType>::Value, "");
	using UnderType = __underlying_type(EnumType);
	return reinterpret_cast<UnderType&>(Type);
}

// Set to 1 to debug ShaderCompileWorker.exe. Set a breakpoint in LaunchWorker() to get the cmd-line.
#define DEBUG_SHADERCOMPILEWORKER 0

// Default value comes from bPromptToRetryFailedShaderCompiles in BaseEngine.ini
// This is set as a global variable to allow changing in the debugger even in release
// For example if there are a lot of content shader compile errors you want to skip over without relaunching
bool GRetryShaderCompilation = false;

static FShaderCompilingManager::EDumpShaderDebugInfo GDumpShaderDebugInfo = FShaderCompilingManager::EDumpShaderDebugInfo::Never;
static FAutoConsoleVariableRef CVarDumpShaderDebugInfo(
	TEXT("r.DumpShaderDebugInfo"),
	CastEnumToUnderlyingTypeReference(GDumpShaderDebugInfo),
	TEXT("Dumps debug info for compiled shaders to GameName/Saved/ShaderDebugInfo\n")
	TEXT("When set to 1, debug info is dumped for all compiled shader\n")
	TEXT("When set to 2, it is restricted to shaders with compilation errors\n")
	TEXT("When set to 3, it is restricted to shaders with compilation errors or warnings\n")
	TEXT("The debug info is platform dependent, but usually includes a preprocessed version of the shader source.\n")
	TEXT("Global shaders automatically dump debug info if r.ShaderDevelopmentMode is enabled, this cvar is not necessary.\n")
	TEXT("On iOS, if the PowerVR graphics SDK is installed to the default path, the PowerVR shader compiler will be called and errors will be reported during the cook.")
	);

static int32 GDumpShaderDebugInfoShort = 0;
static FAutoConsoleVariableRef CVarDumpShaderDebugShortNames(
	TEXT("r.DumpShaderDebugShortNames"),
	GDumpShaderDebugInfoShort,
	TEXT("Only valid when r.DumpShaderDebugInfo > 0.\n")
	TEXT("When set to 1, will shorten names factory and shader type folder names to avoid issues with long paths.")
	);

static int32 GDumpShaderDebugInfoSCWCommandLine = 0;
static FAutoConsoleVariableRef CVarDumpShaderDebugSCWCommandLine(
	TEXT("r.DumpShaderDebugWorkerCommandLine"),
	GDumpShaderDebugInfoSCWCommandLine,
	TEXT("Only valid when r.DumpShaderDebugInfo > 0.\n")
	TEXT("When set to 1, it will generate a file that can be used with ShaderCompileWorker's -directcompile.")
	);

static int32 GShaderMapCompilationTimeout = 2 * 60 * 60;	// anything below an hour can hit a false positive
static FAutoConsoleVariableRef CVarShaderMapCompilationTimeout(
	TEXT("r.ShaderCompiler.ShadermapCompilationTimeout"),
	GShaderMapCompilationTimeout,
	TEXT("Maximum number of seconds a single shadermap (which can be comprised of multiple jobs) can be compiled after being considered hung.")
);

static int32 GCrashOnHungShaderMaps = 0;
static FAutoConsoleVariableRef CVarCrashOnHungShaderMaps(
	TEXT("r.ShaderCompiler.CrashOnHungShaderMaps"),
	GCrashOnHungShaderMaps,
	TEXT("If set to 1, the shader compiler will crash on hung shadermaps.")
);

static int32 GLogShaderCompilerStats = 0;
static FAutoConsoleVariableRef CVarLogShaderCompilerStats(
	TEXT("r.LogShaderCompilerStats"),
	GLogShaderCompilerStats,
	TEXT("When set to 1, Log detailed shader compiler stats.")
);


static int32 GShowShaderWarnings = 0;
static FAutoConsoleVariableRef CVarShowShaderWarnings(
	TEXT("r.ShowShaderCompilerWarnings"),
	GShowShaderWarnings,
	TEXT("When set to 1, will display all warnings.")
	);

static int32 GForceAllCoresForShaderCompiling = 0;
static FAutoConsoleVariableRef CVarForceAllCoresForShaderCompiling(
	TEXT("r.ForceAllCoresForShaderCompiling"),
	GForceAllCoresForShaderCompiling,
	TEXT("When set to 1, it will ignore INI settings and launch as many ShaderCompileWorker instances as cores are available.\n")
	TEXT("Improves shader throughput but for big projects it can make the machine run OOM")
);

static TAutoConsoleVariable<int32> CVarKeepShaderDebugData(
	TEXT("r.Shaders.KeepDebugInfo"),
	0,
	TEXT("Whether to keep shader reflection and debug data from shader bytecode, default is to strip.  When using graphical debuggers like Nsight it can be useful to enable this on startup.")
	TEXT("For some platforms this cvar can be overriden in the Engine.ini, under the [ShaderCompiler] section."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarExportShaderDebugData(
	TEXT("r.Shaders.ExportDebugInfo"),
	0,
	TEXT("Whether to export the shader reflection and debug data from shader bytecode as separate files.")
	TEXT("r.Shaders.KeepDebugInfo must be enabled and r.DumpShaderDebugInfo will enable this cvar.")
	TEXT("For some platforms this cvar can be overriden in the Engine.ini, under the [ShaderCompiler] section."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarExportShaderDebugDataMode(
	TEXT("r.Shaders.ExportDebugInfoMode"),
	0,
	TEXT(" 0: Export as loose files.\n")
	TEXT(" 1: Export as an uncompressed archive.\n"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarOptimizeShaders(
	TEXT("r.Shaders.Optimize"),
	1,
	TEXT("Whether to optimize shaders.  When using graphical debuggers like Nsight it can be useful to disable this on startup."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarShaderFastMath(
	TEXT("r.Shaders.FastMath"),
	1,
	TEXT("Whether to use fast-math optimisations in shaders."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarShaderZeroInitialise(
	TEXT("r.Shaders.ZeroInitialise"),
	1,
	TEXT("Whether to enforce zero initialise local variables of primitive type in shaders. Defaults to 1 (enabled). Not all shader languages can omit zero initialisation."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarShaderBoundsChecking(
	TEXT("r.Shaders.BoundsChecking"),
	1,
	TEXT("Whether to enforce bounds-checking & flush-to-zero/ignore for buffer reads & writes in shaders. Defaults to 1 (enabled). Not all shader languages can omit bounds checking."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarShaderFlowControl(
	TEXT("r.Shaders.FlowControlMode"),
	0,
	TEXT("Specifies whether the shader compiler should preserve or unroll flow-control in shader code.\n")
	TEXT("This is primarily a debugging aid and will override any per-shader or per-material settings if not left at the default value (0).\n")
	TEXT("\t0: Off (Default) - Entirely at the discretion of the platform compiler or the specific shader/material.\n")
	TEXT("\t1: Prefer - Attempt to preserve flow-control.\n")
	TEXT("\t2: Avoid - Attempt to unroll and flatten flow-control.\n"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarD3DRemoveUnusedInterpolators(
	TEXT("r.D3D.RemoveUnusedInterpolators"),
	1,
	TEXT("Enables removing unused interpolators mode when compiling pipelines for D3D.\n")
	TEXT(" -1: Do not actually remove, but make the app think it did (for debugging)\n")
	TEXT(" 0: Disable (default)\n")
	TEXT(" 1: Enable removing unused"),
	ECVF_ReadOnly
	);

static TAutoConsoleVariable<int32> CVarD3DCheckedForTypedUAVs(
	TEXT("r.D3D.CheckedForTypedUAVs"),
	1,
	TEXT("Whether to disallow usage of typed UAV loads, as they are unavailable in Windows 7 D3D 11.0.\n")
	TEXT(" 0: Allow usage of typed UAV loads.\n")
	TEXT(" 1: Disallow usage of typed UAV loads. (default)"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarD3DForceDXC(
	TEXT("r.D3D.ForceDXC"),
	0,
	TEXT("Forces DirectX Shader Compiler (DXC) to be used for all D3D shaders. Shaders compiled with this option are only compatible with D3D12.\n")
	TEXT(" 0: Disable (default)\n")
	TEXT(" 1: Force new compiler for all shaders"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarD3DForceShaderConductorDXCRewrite(
	TEXT("r.D3D.ForceShaderConductorDXCRewrite"),
	0,
	TEXT("Forces rewriting using ShaderConductor when DXC is enabled.\n")
	TEXT(" 0: Do not rewrite (default)\n")
	TEXT(" 1: Force ShaderConductor rewrite"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarOpenGLForceDXC(
	TEXT("r.OpenGL.ForceDXC"),
	0,
	TEXT("Forces DirectX Shader Compiler (DXC) to be used for all OpenGL shaders instead of hlslcc.\n")
	TEXT(" 0: Disable (default)\n")
	TEXT(" 1: Force new compiler for all shaders"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarVulkanForceDXC(
	TEXT("r.Vulkan.ForceDXC"),
	1,
	TEXT("Forces DirectX Shader Compiler (DXC) to be used for all Vulkan shaders instead of hlslcc.\n")
	TEXT(" 0: Disable (hlslcc/glslang)\n")
	TEXT(" 1: Enabled on desktop platforms only (default)\n")
	TEXT(" 2: Enabled on mobile platforms only\n")
	TEXT(" 3: Enabled on all platforms"),
	ECVF_ReadOnly);

int32 GCreateShadersOnLoad = 0;
static FAutoConsoleVariableRef CVarCreateShadersOnLoad(
	TEXT("r.CreateShadersOnLoad"),
	GCreateShadersOnLoad,
	TEXT("Whether to create shaders on load, which can reduce hitching, but use more memory.  Otherwise they will be created as needed.")
);

static TAutoConsoleVariable<FString> CVarShaderOverrideDebugDir(
	TEXT("r.OverrideShaderDebugDir"),
	"",
	TEXT("Override output location of shader debug files\n")
	TEXT("Empty: use default location Saved\\ShaderDebugInfo.\n"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarShadersValidation(
	TEXT("r.Shaders.Validation"),
	1,
	TEXT("Enabled shader compiler validation warnings and errors."),
	ECVF_ReadOnly);

extern bool CompileShaderPipeline(const IShaderFormat* Compiler, FName Format, FShaderPipelineCompileJob* PipelineJob, const FString& Dir);

#if ENABLE_COOK_STATS
#include "ProfilingDebugging/ScopedTimers.h"
namespace ShaderCompilerCookStats
{
	static double BlockingTimeSec = 0.0;
	static double GlobalBeginCompileShaderTimeSec = 0.0;
	static int32 GlobalBeginCompileShaderCalls = 0;
	static double ProcessAsyncResultsTimeSec = 0.0;
	static double AsyncCompileTimeSec = 0.0;

	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		AddStat(TEXT("ShaderCompiler"), FCookStatsManager::CreateKeyValueArray(
			TEXT("BlockingTimeSec"), BlockingTimeSec,
			TEXT("AsyncCompileTimeSec"), AsyncCompileTimeSec,
			TEXT("GlobalBeginCompileShaderTimeSec"), GlobalBeginCompileShaderTimeSec,
			TEXT("GlobalBeginCompileShaderCalls"), GlobalBeginCompileShaderCalls,
			TEXT("ProcessAsyncResultsTimeSec"), ProcessAsyncResultsTimeSec
			));
	});
}
#endif

// Make functions so the crash reporter can disambiguate the actual error because of the different callstacks
namespace SCWErrorCode
{
	void HandleGeneralCrash(const TCHAR* ExceptionInfo, const TCHAR* Callstack)
	{
		GLog->PanicFlushThreadedLogs();
		UE_LOG(LogShaderCompilers, Fatal, TEXT("ShaderCompileWorker crashed!\n%s\n\t%s"), ExceptionInfo, Callstack);
	}

	void HandleBadShaderFormatVersion(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleBadInputVersion(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleBadSingleJobHeader(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleBadPipelineJobHeader(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleCantDeleteInputFile(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleCantSaveOutputFile(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleNoTargetShaderFormatsFound(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleCantCompileForSpecificFormat(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("ShaderCompileWorker failed:\n%s\n"), Data));
	}

	void HandleOutputFileEmpty(const TCHAR* Filename)
	{
		ModalErrorOrLog(FString::Printf(TEXT("Output file %s size is 0. Are you out of disk space?"), Filename));
	}

	void HandleOutputFileCorrupted(const TCHAR* Filename, int64 ExpectedSize, int64 ActualSize)
	{
		ModalErrorOrLog(FString::Printf(TEXT("Output file corrupted (expected %I64d bytes, but only got %I64d): %s"), ExpectedSize, ActualSize, Filename));
	}

	void HandleCrashInsidePlatformCompiler(const TCHAR* Data)
	{
		ModalErrorOrLog(FString::Printf(TEXT("Crash inside the platform compiler!\n%s"), Data));
	}
}

static TMap<FString, uint32> GetFormatVersionMap()
{
	TMap<FString, uint32> FormatVersionMap;

	const TArray<const class IShaderFormat*>& ShaderFormats = GetTargetPlatformManagerRef().GetShaderFormats();
	check(ShaderFormats.Num());
	for (int32 Index = 0; Index < ShaderFormats.Num(); Index++)
	{
		TArray<FName> OutFormats;
		ShaderFormats[Index]->GetSupportedFormats(OutFormats);
		check(OutFormats.Num());
		for (int32 InnerIndex = 0; InnerIndex < OutFormats.Num(); InnerIndex++)
		{
			uint32 Version = ShaderFormats[Index]->GetVersion(OutFormats[InnerIndex]);
			FormatVersionMap.Add(OutFormats[InnerIndex].ToString(), Version);
		}
	}

	return FormatVersionMap;
}

static int32 GetNumTotalJobs(const TArray<FShaderCommonCompileJobPtr>& Jobs)
{
	int32 NumJobs = 0;
	for (int32 Index = 0; Index < Jobs.Num(); ++Index)
	{
		auto* PipelineJob = Jobs[Index]->GetShaderPipelineJob();
		NumJobs += PipelineJob ? PipelineJob->StageJobs.Num() : 1;
	}

	return NumJobs;
}

static void SplitJobsByType(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, TArray<FShaderCompileJob*>& OutQueuedSingleJobs, TArray<FShaderPipelineCompileJob*>& OutQueuedPipelineJobs)
{
	for (int32 Index = 0; Index < QueuedJobs.Num(); ++Index)
	{
		FShaderCommonCompileJobPtr CommonJob = QueuedJobs[Index];
		FShaderPipelineCompileJob* PipelineJob = CommonJob->GetShaderPipelineJob();
		if (PipelineJob)
		{
			OutQueuedPipelineJobs.Add(PipelineJob);
		}
		else
		{
			FShaderCompileJob* SingleJob = CommonJob->GetSingleShaderJob();
			check(SingleJob);
			OutQueuedSingleJobs.Add(SingleJob);
		}
	}
}

// Serialize Queued Job information
bool FShaderCompileUtilities::DoWriteTasks(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& TransferFile, bool bUseRelativePaths)
{
	int32 InputVersion = ShaderCompileWorkerInputVersion;
	TransferFile << InputVersion;

	static TMap<FString, uint32> FormatVersionMap = GetFormatVersionMap();

	TransferFile << FormatVersionMap;

	// Convert all the source directory paths to absolute, since SCW might be in a different directory to the editor executable
	TMap<FString, FString> ShaderSourceDirectoryMappings = AllShaderSourceDirectoryMappings();
	if (!bUseRelativePaths)
	{
		for(TPair<FString, FString>& Pair : ShaderSourceDirectoryMappings)
		{
			Pair.Value = FPaths::ConvertRelativePathToFull(Pair.Value);
		}
	}
	TransferFile << ShaderSourceDirectoryMappings;

	TArray<FShaderCompileJob*> QueuedSingleJobs;
	TArray<FShaderPipelineCompileJob*> QueuedPipelineJobs;
	SplitJobsByType(QueuedJobs, QueuedSingleJobs, QueuedPipelineJobs);

	TArray<TRefCountPtr<FSharedShaderCompilerEnvironment>> SharedEnvironments;

	// Gather External Includes and serialize separately, these are largely shared between jobs
	{
		TMap<FString, FString> ExternalIncludes;
		ExternalIncludes.Reserve(32);

		for (int32 JobIndex = 0; JobIndex < QueuedSingleJobs.Num(); JobIndex++)
		{
			QueuedSingleJobs[JobIndex]->Input.GatherSharedInputs(ExternalIncludes, SharedEnvironments);
		}

		for (int32 JobIndex = 0; JobIndex < QueuedPipelineJobs.Num(); JobIndex++)
		{
			auto* PipelineJob = QueuedPipelineJobs[JobIndex];
			int32 NumStageJobs = PipelineJob->StageJobs.Num();

			for (int32 Index = 0; Index < NumStageJobs; Index++)
			{
				PipelineJob->StageJobs[Index]->Input.GatherSharedInputs(ExternalIncludes, SharedEnvironments);
			}
		}

		int32 NumExternalIncludes = ExternalIncludes.Num();
		TransferFile << NumExternalIncludes;

		for (TMap<FString, FString>::TIterator It(ExternalIncludes); It; ++It)
		{
			TransferFile << It.Key();
			TransferFile << It.Value();
		}

		int32 NumSharedEnvironments = SharedEnvironments.Num();
		TransferFile << NumSharedEnvironments;

		for (int32 EnvironmentIndex = 0; EnvironmentIndex < SharedEnvironments.Num(); EnvironmentIndex++)
		{
			TransferFile << *SharedEnvironments[EnvironmentIndex];
		}
	}

	// Write individual shader jobs
	{
		int32 SingleJobHeader = ShaderCompileWorkerSingleJobHeader;
		TransferFile << SingleJobHeader;

		int32 NumBatches = QueuedSingleJobs.Num();
		TransferFile << NumBatches;

		// Serialize all the batched jobs
		for (int32 JobIndex = 0; JobIndex < QueuedSingleJobs.Num(); JobIndex++)
		{
			TransferFile << QueuedSingleJobs[JobIndex]->Input;
			QueuedSingleJobs[JobIndex]->Input.SerializeSharedInputs(TransferFile, SharedEnvironments);
		}
	}

	// Write shader pipeline jobs
	{
		int32 PipelineJobHeader = ShaderCompileWorkerPipelineJobHeader;
		TransferFile << PipelineJobHeader;

		int32 NumBatches = QueuedPipelineJobs.Num();
		TransferFile << NumBatches;
		for (int32 JobIndex = 0; JobIndex < QueuedPipelineJobs.Num(); JobIndex++)
		{
			auto* PipelineJob = QueuedPipelineJobs[JobIndex];
			FString PipelineName = PipelineJob->Key.ShaderPipeline->GetName();
			TransferFile << PipelineName;
			int32 NumStageJobs = PipelineJob->StageJobs.Num();
			TransferFile << NumStageJobs;
			for (int32 Index = 0; Index < NumStageJobs; Index++)
			{
				TransferFile << PipelineJob->StageJobs[Index]->GetSingleShaderJob()->Input;
				PipelineJob->StageJobs[Index]->Input.SerializeSharedInputs(TransferFile, SharedEnvironments);
			}
		}
	}

	return TransferFile.Close();
}

static void ProcessErrors(const FShaderCompileJob& CurrentJob, TArray<FString>& UniqueErrors, FString& ErrorString)
{
	for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
	{
		FShaderCompilerError CurrentError = CurrentJob.Output.Errors[ErrorIndex];
		int32 UniqueError = INDEX_NONE;

		if (UniqueErrors.Find(CurrentError.GetErrorString(), UniqueError))
		{
			// This unique error is being processed, remove it from the array
			UniqueErrors.RemoveAt(UniqueError);

			// Remap filenames
			if (CurrentError.ErrorVirtualFilePath == TEXT("/Engine/Generated/Material.ush"))
			{
				// MaterialTemplate.usf is dynamically included as Material.usf
				// Currently the material translator does not add new lines when filling out MaterialTemplate.usf,
				// So we don't need the actual filled out version to find the line of a code bug.
				CurrentError.ErrorVirtualFilePath = TEXT("/Engine/Private/MaterialTemplate.ush");
			}
			else if (CurrentError.ErrorVirtualFilePath.Contains(TEXT("memory")))
			{
				check(CurrentJob.Key.ShaderType);

				// Files passed to the shader compiler through memory will be named memory
				// Only the shader's main file is passed through memory without a filename
				CurrentError.ErrorVirtualFilePath = FString(CurrentJob.Key.ShaderType->GetShaderFilename());
			}
			else if (CurrentError.ErrorVirtualFilePath == TEXT("/Engine/Generated/VertexFactory.ush"))
			{
				// VertexFactory.usf is dynamically included from whichever vertex factory the shader was compiled with.
				check(CurrentJob.Key.VFType);
				CurrentError.ErrorVirtualFilePath = FString(CurrentJob.Key.VFType->GetShaderFilename());
			}
			else if (CurrentError.ErrorVirtualFilePath == TEXT("") && CurrentJob.Key.ShaderType)
			{
				// Some shader compiler errors won't have a file and line number, so we just assume the error happened in file containing the entrypoint function.
				CurrentError.ErrorVirtualFilePath = FString(CurrentJob.Key.ShaderType->GetShaderFilename());
			}

			FString UniqueErrorPrefix;

			if (CurrentJob.Key.ShaderType)
			{
				// Construct a path that will enable VS.NET to find the shader file, relative to the solution
				const FString SolutionPath = FPaths::RootDir();
				FString ShaderFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*CurrentError.GetShaderSourceFilePath());
				UniqueErrorPrefix = FString::Printf(TEXT("%s(%s): Shader %s, Permutation %d, VF %s:\n\t"),
					*ShaderFilePath,
					*CurrentError.ErrorLineString,
					CurrentJob.Key.ShaderType->GetName(),
					CurrentJob.Key.PermutationId,
					CurrentJob.Key.VFType ? CurrentJob.Key.VFType->GetName() : TEXT("None"));
			}
			else
			{
				UniqueErrorPrefix = FString::Printf(TEXT("%s(0): "),
					*CurrentJob.Input.VirtualSourceFilePath);
			}

			FString UniqueErrorString = UniqueErrorPrefix + CurrentError.StrippedErrorMessage + TEXT("\n");

			if (GIsBuildMachine)
			{
				// Format everything on one line, and with the correct verbosity, so we can display proper errors in the failure logs.
				UE_LOG(LogShaderCompilers, Error, TEXT("%s%s"), *UniqueErrorPrefix.Replace(TEXT("\n"), TEXT("")), *CurrentError.StrippedErrorMessage);
			}
			else if (FPlatformMisc::IsDebuggerPresent() && !GIsBuildMachine)
			{
				// Using OutputDebugString to avoid any text getting added before the filename,
				// Which will throw off VS.NET's ability to take you directly to the file and line of the error when double clicking it in the output window.
				FPlatformMisc::LowLevelOutputDebugStringf(*UniqueErrorString);
			}
			else
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("%s"), *UniqueErrorString);
			}

			ErrorString += UniqueErrorString;
		}
	}
}

static bool ReadSingleJob(FShaderCompileJob* CurrentJob, FArchive& OutputFile)
{
	check(!CurrentJob->bFinalized);
	CurrentJob->bFinalized = true;

	// Deserialize the shader compilation output.
	OutputFile << CurrentJob->Output;

	// Generate a hash of the output and cache it
	// The shader processing this output will use it to search for existing FShaderResources
	CurrentJob->Output.GenerateOutputHash();
	CurrentJob->bSucceeded = CurrentJob->Output.bSucceeded;

	if (CurrentJob->bSucceeded && CurrentJob->Input.DumpDebugInfoPath.Len() > 0)
	{
		// write down the output hash as a file
		FString HashFileName = FPaths::Combine(CurrentJob->Input.DumpDebugInfoPath, TEXT("OutputHash.txt"));
		FFileHelper::SaveStringToFile(CurrentJob->Output.OutputHash.ToString(), *HashFileName, FFileHelper::EEncodingOptions::ForceAnsi);
	}

	// Support dumping debug info for only failed compilations or those with warnings
	if (GShaderCompilingManager->ShouldRecompileToDumpShaderDebugInfo(*CurrentJob))
	{
		// Build debug info path and create the directory if it doesn't already exist
		CurrentJob->Input.DumpDebugInfoPath = GShaderCompilingManager->CreateShaderDebugInfoPath(CurrentJob->Input);
		return true;
	}

	return false;
}

static FString GetSingleJobCompilationDump(const FShaderCompileJob* SingleJob)
{
	if (!SingleJob)
	{
		return TEXT("Internal error, not a Job!");
	}
	FString String = SingleJob->Input.GenerateShaderName();
	if (SingleJob->Key.VFType)
	{
		String += FString::Printf(TEXT(" VF '%s'"), SingleJob->Key.VFType->GetName());
	}
	String += FString::Printf(TEXT(" Type '%s'"), SingleJob->Key.ShaderType->GetName());
	String += FString::Printf(TEXT(" '%s' Entry '%s' Permutation %i "), *SingleJob->Input.VirtualSourceFilePath, *SingleJob->Input.EntryPointName, SingleJob->Key.PermutationId);
	return String;
}


static void DumpCompilationJobs(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, int32 NumProcessedJobs)
{
	if (NumProcessedJobs == -1)
	{
		UE_LOG(LogShaderCompilers, Error, TEXT("SCW %d Queued Jobs, Unknown number of processed jobs!"), QueuedJobs.Num());
	}
	else
	{
		UE_LOG(LogShaderCompilers, Error, TEXT("SCW %d Queued Jobs, Finished %d single jobs"), QueuedJobs.Num(), NumProcessedJobs);
	}

	for (int32 Index = 0; Index < QueuedJobs.Num(); ++Index)
	{
		if (FShaderCompileJob* SingleJob = QueuedJobs[Index]->GetSingleShaderJob())
		{
			UE_LOG(LogShaderCompilers, Error, TEXT("Job %d [Single] %s"), Index, *GetSingleJobCompilationDump(SingleJob));
		}
		else
		{
			FShaderPipelineCompileJob* PipelineJob = QueuedJobs[Index]->GetShaderPipelineJob();
			UE_LOG(LogShaderCompilers, Error, TEXT("Job %d: Pipeline %s "), Index, PipelineJob->Key.ShaderPipeline->GetName());
			for (int32 JobIndex = 0; JobIndex < PipelineJob->StageJobs.Num(); ++JobIndex)
			{
				UE_LOG(LogShaderCompilers, Error, TEXT("PipelineJob %d %s"), JobIndex, *GetSingleJobCompilationDump(PipelineJob->StageJobs[JobIndex]->GetSingleShaderJob()));
			}
		}
	}

	// Force a log flush so we can track the crash before the cooker potentially crashes before the output shows up
	GLog->PanicFlushThreadedLogs();
}

// Disable optimization for this crash handler to get full access to the entire stack frame when debugging a crash dump
PRAGMA_DISABLE_OPTIMIZATION;
static void HandleWorkerCrash(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& OutputFile, int32 OutputVersion, int64 FileSize, ESCWErrorCode ErrorCode, int32 NumProcessedJobs, int32 CallstackLength, int32 ExceptionInfoLength)
{
	TArray<TCHAR> Callstack;
	Callstack.AddUninitialized(CallstackLength + 1);
	OutputFile.Serialize(Callstack.GetData(), CallstackLength * sizeof(TCHAR));
	Callstack[CallstackLength] = 0;

	TArray<TCHAR> ExceptionInfo;
	ExceptionInfo.AddUninitialized(ExceptionInfoLength + 1);
	OutputFile.Serialize(ExceptionInfo.GetData(), ExceptionInfoLength * sizeof(TCHAR));
	ExceptionInfo[ExceptionInfoLength] = 0;

	// Store primary job information onto stack to make it part of a crash dump
	static const int32 MaxNumCharsForSourcePaths = 8192;
	int32 JobInputSourcePathsLength = 0;
	ANSICHAR JobInputSourcePaths[MaxNumCharsForSourcePaths];
	JobInputSourcePaths[0] = 0;

	auto WriteInputSourcePathOntoStack = [&JobInputSourcePathsLength, &JobInputSourcePaths](const ANSICHAR* InputSourcePath)
	{
		if (InputSourcePath != nullptr && JobInputSourcePathsLength + 3 < MaxNumCharsForSourcePaths)
		{
			// Copy input source path into stack buffer
			int32 InputSourcePathLength = FMath::Min(FCStringAnsi::Strlen(InputSourcePath), (MaxNumCharsForSourcePaths - JobInputSourcePathsLength - 2));
			FMemory::Memcpy(JobInputSourcePaths + JobInputSourcePathsLength, InputSourcePath, InputSourcePathLength);

			// Write newline character and put NUL character at the end
			JobInputSourcePathsLength += InputSourcePathLength;
			JobInputSourcePaths[JobInputSourcePathsLength] = TEXT('\n');
			++JobInputSourcePathsLength;
			JobInputSourcePaths[JobInputSourcePathsLength] = 0;
		}
	};

	auto StoreInputDebugInfo = [&WriteInputSourcePathOntoStack, &JobInputSourcePathsLength, &JobInputSourcePaths](const FShaderCompilerInput& Input)
	{
		FString DebugInfo = FString::Printf(TEXT("%s:%s"), *Input.VirtualSourceFilePath, *Input.EntryPointName);
		WriteInputSourcePathOntoStack(TCHAR_TO_UTF8(*DebugInfo));
	};

	for (auto CommonJob : QueuedJobs)
	{
		if (FShaderCompileJob* SingleJob = CommonJob->GetSingleShaderJob())
		{
			StoreInputDebugInfo(SingleJob->Input);
		}
		else if (FShaderPipelineCompileJob* PipelineJob = CommonJob->GetShaderPipelineJob())
		{
			for (int32 Job = 0; Job < PipelineJob->StageJobs.Num(); ++Job)
			{
				if (FShaderCompileJob* SingleStageJob = PipelineJob->StageJobs[Job])
				{
					StoreInputDebugInfo(SingleStageJob->Input);
				}
			}
		}
	}

	// One entry per error code as we want to have different callstacks for crash reporter...
	switch (ErrorCode)
	{
	default:
	case ESCWErrorCode::GeneralCrash:
	{
		DumpCompilationJobs(QueuedJobs, NumProcessedJobs);
		SCWErrorCode::HandleGeneralCrash(ExceptionInfo.GetData(), Callstack.GetData());
	}
	break;
	case ESCWErrorCode::BadShaderFormatVersion:
		SCWErrorCode::HandleBadShaderFormatVersion(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::BadInputVersion:
		SCWErrorCode::HandleBadInputVersion(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::BadSingleJobHeader:
		SCWErrorCode::HandleBadSingleJobHeader(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::BadPipelineJobHeader:
		SCWErrorCode::HandleBadPipelineJobHeader(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::CantDeleteInputFile:
		SCWErrorCode::HandleCantDeleteInputFile(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::CantSaveOutputFile:
		SCWErrorCode::HandleCantSaveOutputFile(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::NoTargetShaderFormatsFound:
		SCWErrorCode::HandleNoTargetShaderFormatsFound(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::CantCompileForSpecificFormat:
		SCWErrorCode::HandleCantCompileForSpecificFormat(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::CrashInsidePlatformCompiler:
		DumpCompilationJobs(QueuedJobs, NumProcessedJobs);
		SCWErrorCode::HandleCrashInsidePlatformCompiler(ExceptionInfo.GetData());
		break;
	case ESCWErrorCode::Success:
		// Can't get here...
		break;
	}
}
PRAGMA_ENABLE_OPTIMIZATION;

// Process results from Worker Process
void FShaderCompileUtilities::DoReadTaskResults(const TArray<FShaderCommonCompileJobPtr>& QueuedJobs, FArchive& OutputFile)
{
	if (OutputFile.TotalSize() == 0)
	{
		SCWErrorCode::HandleOutputFileEmpty(*OutputFile.GetArchiveName());
	}

	int32 OutputVersion = ShaderCompileWorkerOutputVersion;
	OutputFile << OutputVersion;

	if (ShaderCompileWorkerOutputVersion != OutputVersion)
	{
		FString Text = FString::Printf(TEXT("Expecting ShaderCompileWorker output version %d, got %d instead! Forgot to build ShaderCompileWorker?"), ShaderCompileWorkerOutputVersion, OutputVersion);
		ModalErrorOrLog(Text);
	}

	int64 FileSize = 0;
	OutputFile << FileSize;

	// Check for corrupted output file
	if (FileSize > OutputFile.TotalSize())
	{
		SCWErrorCode::HandleOutputFileCorrupted(*OutputFile.GetArchiveName(), FileSize, OutputFile.TotalSize());
	}

	int32 ErrorCode = 0;
	OutputFile << ErrorCode;

	int32 NumProcessedJobs = 0;
	OutputFile << NumProcessedJobs;

	int32 CallstackLength = 0;
	OutputFile << CallstackLength;

	int32 ExceptionInfoLength = 0;
	OutputFile << ExceptionInfoLength;

	// Worker crashed
	if (ESCWErrorCode(ErrorCode) != ESCWErrorCode::Success)
	{
		HandleWorkerCrash(QueuedJobs, OutputFile, OutputVersion, FileSize, ESCWErrorCode(ErrorCode), NumProcessedJobs, CallstackLength, ExceptionInfoLength);
	}

	TArray<FShaderCompileJob*> QueuedSingleJobs;
	TArray<FShaderPipelineCompileJob*> QueuedPipelineJobs;
	SplitJobsByType(QueuedJobs, QueuedSingleJobs, QueuedPipelineJobs);
	TArray<FShaderCompileJob*> ReissueSourceJobs;

	// Read single jobs
	{
		int32 SingleJobHeader = -1;
		OutputFile << SingleJobHeader;
		if (SingleJobHeader != ShaderCompileWorkerSingleJobHeader)
		{
			FString Text = FString::Printf(TEXT("Expecting ShaderCompileWorker Single Jobs %d, got %d instead! Forgot to build ShaderCompileWorker?"), ShaderCompileWorkerSingleJobHeader, SingleJobHeader);
			ModalErrorOrLog(Text, OutputFile.Tell(), FileSize);
		}

		int32 NumJobs;
		OutputFile << NumJobs;
		if (NumJobs != QueuedSingleJobs.Num())
		{
			FString Text = FString::Printf(TEXT("ShaderCompileWorker returned %u single jobs, %u expected"), NumJobs, QueuedSingleJobs.Num());
			ModalErrorOrLog(Text, OutputFile.Tell(), FileSize);
		}

		for (int32 JobIndex = 0; JobIndex < NumJobs; JobIndex++)
		{
			auto* CurrentJob = QueuedSingleJobs[JobIndex];
			if (ReadSingleJob(CurrentJob, OutputFile))
			{
				ReissueSourceJobs.Add(CurrentJob);
			}
		}
	}

	// Pipeline jobs
	{
		int32 PipelineJobHeader = -1;
		OutputFile << PipelineJobHeader;
		if (PipelineJobHeader != ShaderCompileWorkerPipelineJobHeader)
		{
			FString Text = FString::Printf(TEXT("Expecting ShaderCompileWorker Pipeline Jobs %d, got %d instead! Forgot to build ShaderCompileWorker?"), ShaderCompileWorkerPipelineJobHeader, PipelineJobHeader);
			ModalErrorOrLog(Text, OutputFile.Tell(), FileSize);
		}

		int32 NumJobs;
		OutputFile << NumJobs;
		if (NumJobs != QueuedPipelineJobs.Num())
		{
			FString Text = FString::Printf(TEXT("Worker returned %u pipeline jobs, %u expected"), NumJobs, QueuedPipelineJobs.Num());
			ModalErrorOrLog(Text, OutputFile.Tell(), FileSize);
		}
		for (int32 JobIndex = 0; JobIndex < NumJobs; JobIndex++)
		{
			FShaderPipelineCompileJob* CurrentJob = QueuedPipelineJobs[JobIndex];

			FString PipelineName;
			OutputFile << PipelineName;
			if (PipelineName != CurrentJob->Key.ShaderPipeline->GetName())
			{
				FString Text = FString::Printf(TEXT("Worker returned Pipeline %s, expected %s!"), *PipelineName, CurrentJob->Key.ShaderPipeline->GetName());
				ModalErrorOrLog(Text, OutputFile.Tell(), FileSize);
			}

			check(!CurrentJob->bFinalized);
			CurrentJob->bFinalized = true;
			CurrentJob->bFailedRemovingUnused = false;

			int32 NumStageJobs = -1;
			OutputFile << NumStageJobs;

			if (NumStageJobs != CurrentJob->StageJobs.Num())
			{
				if (NumJobs != QueuedPipelineJobs.Num())
				{
					FString Text = FString::Printf(TEXT("Worker returned %u stage pipeline jobs, %u expected"), NumStageJobs, CurrentJob->StageJobs.Num());
					ModalErrorOrLog(Text, OutputFile.Tell(), FileSize);
				}
			}

			CurrentJob->bSucceeded = true;
			for (int32 Index = 0; Index < NumStageJobs; Index++)
			{
				FShaderCompileJob* SingleJob = CurrentJob->StageJobs[Index];
				// cannot reissue a single stage of a pipeline job
				ReadSingleJob(SingleJob, OutputFile);
				CurrentJob->bFailedRemovingUnused = CurrentJob->bFailedRemovingUnused | SingleJob->Output.bFailedRemovingUnused;
				CurrentJob->bSucceeded = CurrentJob->bSucceeded && SingleJob->bSucceeded;
			}
		}
	}
	
	// Requeue any jobs we wish to run again
	if (ReissueSourceJobs.Num())
	{
		TArray<FShaderCommonCompileJobPtr> ReissueJobs;
		ReissueJobs.Reserve(ReissueSourceJobs.Num());
		const uint32 JobId = FShaderCommonCompileJob::GetNextJobId();
		for (const FShaderCompileJob* ReissueSourceJob : ReissueSourceJobs)
		{
			FShaderCompileJob* ReissueJob = GShaderCompilingManager->PrepareShaderCompileJob(JobId, ReissueSourceJob->Key, ReissueSourceJob->Priority);
			if (ReissueJob)
			{
				ReissueJob->Input = ReissueSourceJob->Input;
				ReissueJobs.Add(FShaderCommonCompileJobPtr(ReissueJob));
			}
		}

		GShaderCompilingManager->SubmitJobs(ReissueJobs, FString(""), FString(""));
	}
}

static bool CheckSingleJob(FShaderCompileJob* SingleJob, TArray<FString>& Errors)
{
	if (SingleJob->bSucceeded)
	{
		check(SingleJob->Output.ShaderCode.GetShaderCodeSize() > 0);
	}

	if (GShowShaderWarnings || !SingleJob->bSucceeded)
	{
		for (int32 ErrorIndex = 0; ErrorIndex < SingleJob->Output.Errors.Num(); ErrorIndex++)
		{
			const FShaderCompilerError& InError = SingleJob->Output.Errors[ErrorIndex];
			Errors.AddUnique(InError.GetErrorStringWithLineMarker());
		}
	}

	bool bSucceeded = SingleJob->bSucceeded;

	if (SingleJob->Key.ShaderType)
	{
		// Allow the shader validation to fail the compile if it sees any parameters bound that aren't supported.
		const bool bValidationResult = SingleJob->Key.ShaderType->ValidateCompiledResult(
			(EShaderPlatform)SingleJob->Input.Target.Platform,
			SingleJob->Output.ParameterMap,
			Errors);
		bSucceeded = bValidationResult && bSucceeded;
	}

	if (SingleJob->Key.VFType)
	{
		const int32 OriginalNumErrors = Errors.Num();

		// Allow the vertex factory to fail the compile if it sees any parameters bound that aren't supported
		SingleJob->Key.VFType->ValidateCompiledResult((EShaderPlatform)SingleJob->Input.Target.Platform, SingleJob->Output.ParameterMap, Errors);

		if (Errors.Num() > OriginalNumErrors)
		{
			bSucceeded = false;
		}
	}

	return bSucceeded;
};

static void AddErrorsForFailedJob(FShaderCompileJob& CurrentJob, TArray<EShaderPlatform>& ErrorPlatforms, TArray<FString>& UniqueErrors, TArray<FShaderCommonCompileJob*>& ErrorJobs)
{
	ErrorPlatforms.AddUnique((EShaderPlatform)CurrentJob.Input.Target.Platform);

	if (CurrentJob.Output.Errors.Num() == 0)
	{
		// Job hard crashed
		FShaderCompilerError Error(*(FString("Internal Error!\n\t") + GetSingleJobCompilationDump(&CurrentJob)));
		CurrentJob.Output.Errors.Add(Error);
	}

	for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
	{
		const FShaderCompilerError& CurrentError = CurrentJob.Output.Errors[ErrorIndex];

		// Include warnings if LogShaders is unsuppressed, otherwise only include errors
		if (UE_LOG_ACTIVE(LogShaders, Log) || CurrentError.StrippedErrorMessage.Contains(TEXT("error")))
		{
			UniqueErrors.AddUnique(CurrentJob.Output.Errors[ErrorIndex].GetErrorString());
			ErrorJobs.AddUnique(&CurrentJob);
		}
	}
}

/** Information tracked for each shader compile worker process instance. */
struct FShaderCompileWorkerInfo
{
	/** Process handle of the worker app once launched.  Invalid handle means no process. */
	FProcHandle WorkerProcess;

	/** Tracks whether tasks have been issued to the worker. */
	bool bIssuedTasksToWorker;	

	/** Whether the worker has been launched for this set of tasks. */
	bool bLaunchedWorker;

	/** Tracks whether all tasks issued to the worker have been received. */
	bool bComplete;

	/** Time at which the worker started the most recent batch of tasks. */
	double StartTime;

	/** Jobs that this worker is responsible for compiling. */
	TArray<FShaderCommonCompileJobPtr> QueuedJobs;

	FShaderCompileWorkerInfo() :
		bIssuedTasksToWorker(false),		
		bLaunchedWorker(false),
		bComplete(false),
		StartTime(0)
	{
	}

	// warning: not virtual
	~FShaderCompileWorkerInfo()
	{
		if(WorkerProcess.IsValid())
		{
			FPlatformProcess::TerminateProc(WorkerProcess);
			FPlatformProcess::CloseProc(WorkerProcess);
		}
	}
};

FShaderCompileThreadRunnableBase::FShaderCompileThreadRunnableBase(FShaderCompilingManager* InManager)
	: Manager(InManager)
	, MinPriorityIndex(0)
	, MaxPriorityIndex(NumShaderCompileJobPriorities - 1)
	, bTerminatedByError(false)
	, bForceFinish(false)
{
}
void FShaderCompileThreadRunnableBase::StartThread()
{
	if (Manager->bAllowAsynchronousShaderCompiling && !FPlatformProperties::RequiresCookedData())
	{
		Thread = FRunnableThread::Create(this, TEXT("ShaderCompilingThread"), 0, TPri_Normal, FPlatformAffinity::GetPoolThreadMask());
	}
}

FShaderCompileThreadRunnable::FShaderCompileThreadRunnable(FShaderCompilingManager* InManager)
	: FShaderCompileThreadRunnableBase(InManager)
	, LastCheckForWorkersTime(0)
{
	for (uint32 WorkerIndex = 0; WorkerIndex < Manager->NumShaderCompilingThreads; WorkerIndex++)
	{
		WorkerInfos.Add(new FShaderCompileWorkerInfo());
	}
}

FShaderCompileThreadRunnable::~FShaderCompileThreadRunnable()
{
	for (int32 Index = 0; Index < WorkerInfos.Num(); Index++)
	{
		delete WorkerInfos[Index];
	}

	WorkerInfos.Empty(0);
}

/** Entry point for the shader compiling thread. */
uint32 FShaderCompileThreadRunnableBase::Run()
{
#ifdef _MSC_VER
	if(!FPlatformMisc::IsDebuggerPresent())
	{
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__try
#endif
		{
			check(Manager->bAllowAsynchronousShaderCompiling);
			// Do the work
			while (!bForceFinish)
			{
				CompilingLoop();
			}
		}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__except( 
#if PLATFORM_WINDOWS
			ReportCrash( GetExceptionInformation() )
#else
			EXCEPTION_EXECUTE_HANDLER
#endif
			)
		{
#if WITH_EDITORONLY_DATA
			ErrorMessage = GErrorHist;
#endif
			// Use a memory barrier to ensure that the main thread sees the write to ErrorMessage before
			// the write to bTerminatedByError.
			FPlatformMisc::MemoryBarrier();

			bTerminatedByError = true;
		}
#endif
	}
	else
#endif
	{
		check(Manager->bAllowAsynchronousShaderCompiling);
		while (!bForceFinish)
		{
			CompilingLoop();
		}
	}
	UE_LOG(LogShaderCompilers, Display, TEXT("Shaders left to compile 0"));

	return 0;
}

/** Called by the main thread only, reports exceptions in the worker threads */
void FShaderCompileThreadRunnableBase::CheckHealth() const
{
	if (bTerminatedByError)
	{
#if WITH_EDITORONLY_DATA
		GErrorHist[0] = 0;
#endif
		GIsCriticalError = false;
		UE_LOG(LogShaderCompilers, Fatal,TEXT("Shader Compiling thread exception:\r\n%s"), *ErrorMessage);
	}
}

int32 FShaderCompileThreadRunnable::PullTasksFromQueue()
{
	int32 NumActiveThreads = 0;
	int32 NumJobsStarted[NumShaderCompileJobPriorities] = { 0 };
	{
		// Enter the critical section so we can access the input and output queues
		FScopeLock Lock(&Manager->CompileQueueSection);

		const int32 NumWorkersToFeed = Manager->bCompilingDuringGame ? Manager->NumShaderCompilingThreadsDuringGame : WorkerInfos.Num();

		for (int32 PriorityIndex = MaxPriorityIndex; PriorityIndex >= MinPriorityIndex ; --PriorityIndex)
		{
			int32 NumPendingJobs = Manager->AllJobs.GetNumPendingJobs((EShaderCompileJobPriority)PriorityIndex);
			// Try to distribute the work evenly between the workers
			const auto NumJobsPerWorker = (NumPendingJobs / NumWorkersToFeed) + 1;

			for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
			{
				FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

				// If this worker doesn't have any queued jobs, look for more in the input queue
				if (CurrentWorkerInfo.QueuedJobs.Num() == 0 && WorkerIndex < NumWorkersToFeed)
				{
					check(!CurrentWorkerInfo.bComplete);

					NumPendingJobs = Manager->AllJobs.GetNumPendingJobs((EShaderCompileJobPriority)PriorityIndex);
					if (NumPendingJobs > 0)
					{
						UE_LOG(LogShaderCompilers, Display, TEXT("Worker (%d/%d): shaders left to compile %i"), WorkerIndex + 1, WorkerInfos.Num(), NumPendingJobs);

						int32 MaxNumJobs = 1;
						//if (PriorityIndex < (int32)EShaderCompileJobPriority::ForceLocal)
						{
							MaxNumJobs = FMath::Min3(NumJobsPerWorker, NumPendingJobs, Manager->MaxShaderJobBatchSize);
						}

						NumJobsStarted[PriorityIndex] += Manager->AllJobs.GetPendingJobs(EShaderCompilerWorkerType::LocalThread, (EShaderCompileJobPriority)PriorityIndex, 1, MaxNumJobs, CurrentWorkerInfo.QueuedJobs);

						// Update the worker state as having new tasks that need to be issued					
						// don't reset worker app ID, because the shadercompileworkers don't shutdown immediately after finishing a single job queue.
						CurrentWorkerInfo.bIssuedTasksToWorker = false;
						CurrentWorkerInfo.bLaunchedWorker = false;
						CurrentWorkerInfo.StartTime = FPlatformTime::Seconds();
						NumActiveThreads++;
					}
				}
			}
		}

		for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
		{
			FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

			if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
			{
				NumActiveThreads++;
			}

			// Add completed jobs to the output queue, which is ShaderMapJobs
			if (CurrentWorkerInfo.bComplete)
			{
				for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
				{
					auto& Job = CurrentWorkerInfo.QueuedJobs[JobIndex];

					Manager->ProcessFinishedJob(Job.GetReference());
				}

				const float ElapsedTime = FPlatformTime::Seconds() - CurrentWorkerInfo.StartTime;

				Manager->WorkersBusyTime += ElapsedTime;
				COOK_STAT(ShaderCompilerCookStats::AsyncCompileTimeSec += ElapsedTime);

				// Log if requested or if there was an exceptionally slow batch, to see the offender easily
				if (Manager->bLogJobCompletionTimes || ElapsedTime > 30.0f)
				{
					FString JobNames;

					for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
					{
						const FShaderCommonCompileJob& Job = *CurrentWorkerInfo.QueuedJobs[JobIndex];
						if (auto* SingleJob = Job.GetSingleShaderJob())
						{
							JobNames += FString(SingleJob->Key.ShaderType->GetName()) + TEXT(" Instructions = ") + FString::FromInt(SingleJob->Output.NumInstructions);
						}
						else
						{
							auto* PipelineJob = Job.GetShaderPipelineJob();
							JobNames += FString(PipelineJob->Key.ShaderPipeline->GetName());
							if (PipelineJob->bFailedRemovingUnused)
							{
								JobNames += FString(TEXT("(failed to optimize)"));
							}
						}
						if (JobIndex < CurrentWorkerInfo.QueuedJobs.Num() - 1)
						{
							JobNames += TEXT(", ");
						}
					}

					UE_LOG(LogShaders, Display, TEXT("Finished batch of %u jobs in %.3fs, %s"), CurrentWorkerInfo.QueuedJobs.Num(), ElapsedTime, *JobNames);
				}

				CurrentWorkerInfo.bComplete = false;
				CurrentWorkerInfo.QueuedJobs.Empty();
			}
		}
	}

	for (int32 PriorityIndex = 0; PriorityIndex < NumShaderCompileJobPriorities; ++PriorityIndex)
	{
		if (NumJobsStarted[PriorityIndex] > 0)
		{
			UE_LOG(LogShaderCompilers, Display, TEXT("Started %d 'Local' shader compile jobs with '%s' priority"),
				NumJobsStarted[PriorityIndex],
				ShaderCompileJobPriorityToString((EShaderCompileJobPriority)PriorityIndex));
		}
	}

	return NumActiveThreads;
}

void FShaderCompileThreadRunnable::WriteNewTasks()
{
	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// Only write tasks once
		if (!CurrentWorkerInfo.bIssuedTasksToWorker && CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			CurrentWorkerInfo.bIssuedTasksToWorker = true;

			const FString WorkingDirectory = Manager->AbsoluteShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex);

			// To make sure that the process waiting for input file won't try to read it until it's ready
			// we use a temp file name during writing.
			FString TransferFileName;
			do
			{
				FGuid Guid;
				FPlatformMisc::CreateGuid(Guid);
				TransferFileName = WorkingDirectory + Guid.ToString();
			} while (IFileManager::Get().FileSize(*TransferFileName) != INDEX_NONE);

			// Write out the file that the worker app is waiting for, which has all the information needed to compile the shader.
			// 'Only' indicates that the worker should keep checking for more tasks after this one
			FArchive* TransferFile = NULL;

			int32 RetryCount = 0;
			// Retry over the next two seconds if we can't write out the input file
			// Anti-virus and indexing applications can interfere and cause this write to fail
			//@todo - switch to shared memory or some other method without these unpredictable hazards
			while (TransferFile == NULL && RetryCount < 2000)
			{
				if (RetryCount > 0)
				{
					FPlatformProcess::Sleep(0.01f);
				}
				TransferFile = IFileManager::Get().CreateFileWriter(*TransferFileName, FILEWRITE_EvenIfReadOnly);
				RetryCount++;
				if (TransferFile == NULL)
				{
					UE_LOG(LogShaderCompilers, Warning, TEXT("Could not create the shader compiler transfer file '%s', retrying..."), *TransferFileName);
				}
			}
			if (TransferFile == NULL)
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Could not create the shader compiler transfer file '%s'."), *TransferFileName);
			}
			check(TransferFile);

			if (!FShaderCompileUtilities::DoWriteTasks(CurrentWorkerInfo.QueuedJobs, *TransferFile))
			{
				uint64 TotalDiskSpace = 0;
				uint64 FreeDiskSpace = 0;
				FPlatformMisc::GetDiskTotalAndFreeSpace(TransferFileName, TotalDiskSpace, FreeDiskSpace);
				UE_LOG(LogShaderCompilers, Error, TEXT("Could not write the shader compiler transfer filename to '%s' (Free Disk Space: %llu."), *TransferFileName, FreeDiskSpace);
			}
			delete TransferFile;

#if 0 // debugging code to dump the worker inputs
			static FCriticalSection ArchiveLock;
			{
				FScopeLock Locker(&ArchiveLock);
				static int ArchivedTransferFileNum = 0;
				FString JobCacheDir = ShaderCompiler::IsJobCacheEnabled() ? TEXT("JobCache") : TEXT("NoJobCache");
				FString ArchiveDir = FPaths::ProjectSavedDir() / TEXT("ArchivedWorkerInputs") / JobCacheDir;
				FString ArchiveName = FString::Printf(TEXT("Input-%d"), ArchivedTransferFileNum++);
				FString ArchivePath = ArchiveDir / ArchiveName;
				if (!IFileManager::Get().Copy(*ArchivePath, *TransferFileName))
				{
					UE_LOG(LogInit, Error, TEXT("Could not copy file %s to %s"), *TransferFileName, *ArchivePath);
					ensure(false);
				}
			}
#endif

			// Change the transfer file name to proper one
			FString ProperTransferFileName = WorkingDirectory / TEXT("WorkerInputOnly.in");
			if (!IFileManager::Get().Move(*ProperTransferFileName, *TransferFileName))
			{
				uint64 TotalDiskSpace = 0;
				uint64 FreeDiskSpace = 0;
				FPlatformMisc::GetDiskTotalAndFreeSpace(TransferFileName, TotalDiskSpace, FreeDiskSpace);
				UE_LOG(LogShaderCompilers, Error, TEXT("Could not rename the shader compiler transfer filename to '%s' from '%s' (Free Disk Space: %llu)."), *ProperTransferFileName, *TransferFileName, FreeDiskSpace);
			}
		}
	}
}

bool FShaderCompileThreadRunnable::LaunchWorkersIfNeeded()
{
	const double CurrentTime = FPlatformTime::Seconds();
	// Limit how often we check for workers running since IsApplicationRunning eats up some CPU time on Windows
	const bool bCheckForWorkerRunning = (CurrentTime - LastCheckForWorkersTime > .1f);
	bool bAbandonWorkers = false;

	if (bCheckForWorkerRunning)
	{
		LastCheckForWorkersTime = CurrentTime;
	}

	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];
		if (CurrentWorkerInfo.QueuedJobs.Num() == 0)
		{
			// Skip if nothing to do
			// Also, use the opportunity to free OS resources by cleaning up handles of no more running processes
			if (CurrentWorkerInfo.WorkerProcess.IsValid() && !FShaderCompilingManager::IsShaderCompilerWorkerRunning(CurrentWorkerInfo.WorkerProcess))
			{
				FPlatformProcess::CloseProc(CurrentWorkerInfo.WorkerProcess);
				CurrentWorkerInfo.WorkerProcess = FProcHandle();
			}
			continue;
		}

		if (!CurrentWorkerInfo.WorkerProcess.IsValid() || (bCheckForWorkerRunning && !FShaderCompilingManager::IsShaderCompilerWorkerRunning(CurrentWorkerInfo.WorkerProcess)))
		{
			// @TODO: dubious design - worker should not be launched unless we know there's more work to do.
			bool bLaunchAgain = true;

			// Detect when the worker has exited due to fatal error
			// bLaunchedWorker check here is necessary to distinguish between 'process isn't running because it crashed' and 'process isn't running because it exited cleanly and the outputfile was already consumed'
			if (CurrentWorkerInfo.WorkerProcess.IsValid())
			{
				// shader compiler exited one way or another, so clear out the stale PID.
				FPlatformProcess::CloseProc(CurrentWorkerInfo.WorkerProcess);
				CurrentWorkerInfo.WorkerProcess = FProcHandle();

				if (CurrentWorkerInfo.bLaunchedWorker)
				{
					const FString WorkingDirectory = Manager->AbsoluteShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex) + TEXT("/");
					const FString OutputFileNameAndPath = WorkingDirectory + TEXT("WorkerOutputOnly.out");

					if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutputFileNameAndPath))
					{
						// If the worker is no longer running but it successfully wrote out the output, no need to assert
						bLaunchAgain = false;
					}
					else
					{
						UE_LOG(LogShaderCompilers, Error, TEXT("ShaderCompileWorker terminated unexpectedly!  Falling back to directly compiling which will be very slow.  Thread %u."), WorkerIndex);
						DumpCompilationJobs(CurrentWorkerInfo.QueuedJobs, -1);

						bAbandonWorkers = true;
						break;
					}
				}
			}

			if (bLaunchAgain)
			{
				const FString WorkingDirectory = Manager->ShaderBaseWorkingDirectory + FString::FromInt(WorkerIndex) + TEXT("/");
				FString InputFileName(TEXT("WorkerInputOnly.in"));
				FString OutputFileName(TEXT("WorkerOutputOnly.out"));

				// Store the handle with this thread so that we will know not to launch it again
				CurrentWorkerInfo.WorkerProcess = Manager->LaunchWorker(WorkingDirectory, Manager->ProcessId, WorkerIndex, InputFileName, OutputFileName);
				CurrentWorkerInfo.bLaunchedWorker = true;
			}
		}
	}

	return bAbandonWorkers;
}

void FShaderCompileThreadRunnable::ReadAvailableResults()
{
	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		// Check for available result files
		if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			// Distributed compiles always use the same directory
			// 'Only' indicates to the worker that it should log and continue checking for the input file after the first one is processed
			TStringBuilder<512> OutputFileNameAndPath;
			OutputFileNameAndPath << Manager->AbsoluteShaderBaseWorkingDirectory << WorkerIndex << TEXT("/WorkerOutputOnly.out");
			
			// In the common case the output file will not exist, so check for existence before opening
			// This is only a win if FileExists is faster than CreateFileReader, which it is on Windows
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutputFileNameAndPath))
			{
				FArchive* OutputFilePtr = IFileManager::Get().CreateFileReader(*OutputFileNameAndPath, FILEREAD_Silent);

				if (OutputFilePtr)
				{
					FArchive& OutputFile = *OutputFilePtr;
					check(!CurrentWorkerInfo.bComplete);
					FShaderCompileUtilities::DoReadTaskResults(CurrentWorkerInfo.QueuedJobs, OutputFile);

					// Close the output file.
					delete OutputFilePtr;

					// Delete the output file now that we have consumed it, to avoid reading stale data on the next compile loop.
					bool bDeletedOutput = IFileManager::Get().Delete(*OutputFileNameAndPath, true, true);
					int32 RetryCount = 0;
					// Retry over the next two seconds if we couldn't delete it
					while (!bDeletedOutput && RetryCount < 200)
					{
						FPlatformProcess::Sleep(0.01f);
						bDeletedOutput = IFileManager::Get().Delete(*OutputFileNameAndPath, true, true);
						RetryCount++;
					}
					checkf(bDeletedOutput, TEXT("Failed to delete %s!"), *OutputFileNameAndPath);

					CurrentWorkerInfo.bComplete = true;
				}
			}
		}
	}
}

void FShaderCompileThreadRunnable::CompileDirectlyThroughDll()
{
	// If we aren't compiling through workers, so we can just track the serial time here.
	COOK_STAT(FScopedDurationTimer CompileTimer (ShaderCompilerCookStats::AsyncCompileTimeSec));

	for (int32 WorkerIndex = 0; WorkerIndex < WorkerInfos.Num(); WorkerIndex++)
	{
		FShaderCompileWorkerInfo& CurrentWorkerInfo = *WorkerInfos[WorkerIndex];

		if (CurrentWorkerInfo.QueuedJobs.Num() > 0)
		{
			for (int32 JobIndex = 0; JobIndex < CurrentWorkerInfo.QueuedJobs.Num(); JobIndex++)
			{
				FShaderCommonCompileJob& CurrentJob = *CurrentWorkerInfo.QueuedJobs[JobIndex];
				FShaderCompileUtilities::ExecuteShaderCompileJob(CurrentJob);
			}

			CurrentWorkerInfo.bComplete = true;
		}
	}
}

void FShaderCompileUtilities::ExecuteShaderCompileJob(FShaderCommonCompileJob& Job)
{
	check(!Job.bFinalized);

	static ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	auto* SingleJob = Job.GetSingleShaderJob();
	if (SingleJob)
	{
		const FName Format = SingleJob->Input.ShaderFormat != NAME_None ? SingleJob->Input.ShaderFormat : LegacyShaderPlatformToShaderFormat(EShaderPlatform(SingleJob->Input.Target.Platform));
		const IShaderFormat* Compiler = TPM.FindShaderFormat(Format);

		if (!Compiler)
		{
			UE_LOG(LogShaderCompilers, Fatal, TEXT("Can't compile shaders for format %s, couldn't load compiler dll"), *Format.ToString());
		}
		CA_ASSUME(Compiler != NULL);

		if (IsValidRef(SingleJob->Input.SharedEnvironment))
		{
			// Merge the shared environment into the per-shader environment before calling into the compile function
			// Normally this happens in the worker
			SingleJob->Input.Environment.Merge(*SingleJob->Input.SharedEnvironment);
		}

		// Compile the shader directly through the platform dll (directly from the shader dir as the working directory)
		Compiler->CompileShader(Format, SingleJob->Input, SingleJob->Output, FString(FPlatformProcess::ShaderDir()));

		SingleJob->bSucceeded = SingleJob->Output.bSucceeded;

		if (SingleJob->Output.bSucceeded)
		{
			// Generate a hash of the output and cache it
			// The shader processing this output will use it to search for existing FShaderResources
			SingleJob->Output.GenerateOutputHash();
		}
	}
	else
	{
		FShaderPipelineCompileJob* PipelineJob = Job.GetShaderPipelineJob();
		check(PipelineJob);

		EShaderPlatform Platform = (EShaderPlatform)PipelineJob->StageJobs[0]->Input.Target.Platform;
		const FName Format = LegacyShaderPlatformToShaderFormat(Platform);
		const IShaderFormat* Compiler = TPM.FindShaderFormat(Format);

		if (!Compiler)
		{
			UE_LOG(LogShaderCompilers, Fatal, TEXT("Can't compile shaders for format %s, couldn't load compiler dll"), *Format.ToString());
		}
		CA_ASSUME(Compiler != NULL);

		// Verify same platform on all stages
		for (int32 Index = 1; Index < PipelineJob->StageJobs.Num(); ++Index)
		{
			auto SingleStage = PipelineJob->StageJobs[Index];
			if (!SingleStage)
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Can't nest Shader Pipelines inside Shader Pipeline '%s'!"), PipelineJob->Key.ShaderPipeline->GetName());
			}
			else if (Platform != SingleStage->Input.Target.Platform)
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Mismatched Target Platform %s while compiling Shader Pipeline '%s'."), *Format.GetPlainNameString(), PipelineJob->Key.ShaderPipeline->GetName());
			}
		}

		CompileShaderPipeline(Compiler, Format, PipelineJob, FString(FPlatformProcess::ShaderDir()));
	}

	Job.bFinalized = true;
}

FArchive* FShaderCompileUtilities::CreateFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	FArchive* File = nullptr;
	int32 RetryCount = 0;
	// Retry over the next two seconds if we can't write out the file.
	// Anti-virus and indexing applications can interfere and cause this to fail.
	while (File == nullptr && RetryCount < 200)
	{
		if (RetryCount > 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly);
		RetryCount++;
	}
	if (File == nullptr)
	{
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly | FILEWRITE_NoFail);
	}
	checkf(File, TEXT("Failed to create file %s!"), *Filename);
	return File;
}

void FShaderCompileUtilities::MoveFileHelper(const FString& To, const FString& From)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*From))
	{
		FString DirectoryName;
		int32 LastSlashIndex;
		if (To.FindLastChar('/', LastSlashIndex))
		{
			DirectoryName = To.Left(LastSlashIndex);
		} else
		{
			DirectoryName = To;
		}

		// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
		// We can't avoid code duplication unless we refactored the local worker too.

		bool Success = false;
		int32 RetryCount = 0;
		// Retry over the next two seconds if we can't move the file.
		// Anti-virus and indexing applications can interfere and cause this to fail.
		while (!Success && RetryCount < 200)
		{
			if (RetryCount > 0)
			{
				FPlatformProcess::Sleep(0.01f);
			}

			// MoveFile does not create the directory tree, so try to do that now...
			Success = PlatformFile.CreateDirectoryTree(*DirectoryName);
			if (Success)
			{
				Success = PlatformFile.MoveFile(*To, *From);
			}
			RetryCount++;
		}
		checkf(Success, TEXT("Failed to move file %s to %s!"), *From, *To);
	}
}

void FShaderCompileUtilities::DeleteFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Filename))
	{
		bool bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);

		// Retry over the next two seconds if we couldn't delete it
		int32 RetryCount = 0;
		while (!bDeletedOutput && RetryCount < 200)
		{
			FPlatformProcess::Sleep(0.01f);
			bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);
			RetryCount++;
		}
		checkf(bDeletedOutput, TEXT("Failed to delete %s!"), *Filename);
	}
}

int32 FShaderCompileThreadRunnable::CompilingLoop()
{
	// Grab more shader compile jobs from the input queue, and move completed jobs to Manager->ShaderMapJobs
	const int32 NumActiveThreads = PullTasksFromQueue();

	if (NumActiveThreads == 0 && Manager->bAllowAsynchronousShaderCompiling)
	{
		// Yield while there's nothing to do
		// Note: sleep-looping is bad threading practice, wait on an event instead!
		// The shader worker thread does it because it needs to communicate with other processes through the file system
		FPlatformProcess::Sleep(.010f);
	}

	if (Manager->bAllowCompilingThroughWorkers)
	{
		// Write out the files which are input to the shader compile workers
		WriteNewTasks();

		// Launch shader compile workers if they are not already running
		// Workers can time out when idle so they may need to be relaunched
		bool bAbandonWorkers = LaunchWorkersIfNeeded();

		if (bAbandonWorkers)
		{
			// Fall back to local compiles if the SCW crashed.
			// This is nasty but needed to work around issues where message passing through files to SCW is unreliable on random PCs
			Manager->bAllowCompilingThroughWorkers = false;

			// Try to recover from abandoned workers after a certain amount of single-threaded compilations
			if (Manager->NumSingleThreadedRunsBeforeRetry == GSingleThreadedRunsIdle)
			{
				// First try to recover, only run single-threaded approach once
				Manager->NumSingleThreadedRunsBeforeRetry = 1;
			}
			else if (Manager->NumSingleThreadedRunsBeforeRetry > GSingleThreadedRunsMaxCount)
			{
				// Stop retry approach after too many retries have failed
				Manager->NumSingleThreadedRunsBeforeRetry = GSingleThreadedRunsDisabled;
			}
			else
			{
				// Next time increase runs by factor X
				Manager->NumSingleThreadedRunsBeforeRetry *= GSingleThreadedRunsIncreaseFactor;
			}
		}
		else
		{
			// Read files which are outputs from the shader compile workers
			ReadAvailableResults();
		}
	}
	else
	{
		// Execute all pending worker tasks single-threaded
		CompileDirectlyThroughDll();

		// If single-threaded mode was enabled by an abandoned worker, try to recover after the given amount of runs
		if (Manager->NumSingleThreadedRunsBeforeRetry > 0)
		{
			Manager->NumSingleThreadedRunsBeforeRetry--;
			if (Manager->NumSingleThreadedRunsBeforeRetry == 0)
			{
				UE_LOG(LogShaderCompilers, Display, TEXT("Retry shader compiling through workers."));
				Manager->bAllowCompilingThroughWorkers = true;
			}
		}
	}

	return NumActiveThreads;
}

FShaderCompilerStats* GShaderCompilerStats = NULL;

void FShaderCompilerStats::WriteStats()
{
#if ALLOW_DEBUG_FILES
	{
		FlushRenderingCommands(true);

		FString FileName = FPaths::Combine(*FPaths::ProjectSavedDir(), FString::Printf(TEXT("MaterialStats/Stats-%s.csv"),  *FDateTime::Now().ToString()));
		auto DebugWriter = IFileManager::Get().CreateFileWriter(*FileName);
		FDiagnosticTableWriterCSV StatWriter(DebugWriter);
		const TSparseArray<ShaderCompilerStats>& PlatformStats = GetShaderCompilerStats();

		StatWriter.AddColumn(TEXT("Path"));
		StatWriter.AddColumn(TEXT("Platform"));
		StatWriter.AddColumn(TEXT("Compiled"));
		StatWriter.AddColumn(TEXT("Cooked"));
		StatWriter.AddColumn(TEXT("Permutations"));
		StatWriter.AddColumn(TEXT("Compiletime"));
		StatWriter.AddColumn(TEXT("CompiledDouble"));
		StatWriter.AddColumn(TEXT("CookedDouble"));
		StatWriter.CycleRow();

		
		for(int32 Platform = 0; Platform < PlatformStats.GetMaxIndex(); ++Platform)
		{
			if(PlatformStats.IsValidIndex(Platform))
			{
				const ShaderCompilerStats& Stats = PlatformStats[Platform];
				for (const auto& Pair : Stats)
				{
					const FString& Path = Pair.Key;
					const FShaderCompilerStats::FShaderStats& SingleStats = Pair.Value;

					StatWriter.AddColumn(*Path);
					StatWriter.AddColumn(TEXT("%u"), Platform);
					StatWriter.AddColumn(TEXT("%u"), SingleStats.Compiled);
					StatWriter.AddColumn(TEXT("%u"), SingleStats.Cooked);
					StatWriter.AddColumn(TEXT("%u"), SingleStats.PermutationCompilations.Num());
					StatWriter.AddColumn(TEXT("%f"), SingleStats.CompileTime);
					StatWriter.AddColumn(TEXT("%u"), SingleStats.CompiledDouble);
					StatWriter.AddColumn(TEXT("%u"), SingleStats.CookedDouble);
					StatWriter.CycleRow();
					if(GLogShaderCompilerStats)
					{
						UE_LOG(LogShaderCompilers, Log, TEXT("SHADERSTATS %s, %u, %u, %u, %u, %u, %u\n"), *Path, Platform, SingleStats.Compiled, SingleStats.Cooked, SingleStats.PermutationCompilations.Num(), SingleStats.CompiledDouble, SingleStats.CookedDouble);
					}
				}
			}
		}
		DebugWriter->Close();
		if (FParse::Param(FCommandLine::Get(), TEXT("mirrorshaderstats")))
		{
			FString MirrorLocation;
			GConfig->GetString(TEXT("/Script/Engine.ShaderCompilerStats"), TEXT("MaterialStatsLocation"), MirrorLocation, GGameIni);
			FParse::Value(FCommandLine::Get(), TEXT("MaterialStatsMirror="), MirrorLocation);

			if (!MirrorLocation.IsEmpty())
			{
				FString TargetType = TEXT("Default");
				FParse::Value(FCommandLine::Get(), TEXT("target="), TargetType);
				if (TargetType == TEXT("Default"))
				{
					FParse::Value(FCommandLine::Get(), TEXT("targetplatform="), TargetType);
				}
				FString CopyLocation = FPaths::Combine(*MirrorLocation, FApp::GetProjectName(), *FApp::GetBranchName(), FString::Printf(TEXT("Stats-Latest-%d(%s).csv"), FEngineVersion::Current().GetChangelist() , *TargetType));
				TArray <FString> ExistingFiles;
				IFileManager::Get().FindFiles(ExistingFiles, *FPaths::Combine(*MirrorLocation, FApp::GetProjectName(), *FApp::GetBranchName()));
				for (FString CurFile : ExistingFiles)
				{
					if (CurFile.Contains(FString::Printf(TEXT("(%s)"), *TargetType)))
					{
						IFileManager::Get().Delete(*FPaths::Combine(*MirrorLocation, FApp::GetProjectName(), *FApp::GetBranchName(), *CurFile), false, true);
					}
				}
				IFileManager::Get().Copy(*CopyLocation, *FileName, true, true);
			}
		}
	}
	{

		FString FileName = FString::Printf(TEXT("%s/MaterialStatsDebug/StatsDebug-%s.csv"), *FPaths::ProjectSavedDir(), *FDateTime::Now().ToString());
		auto DebugWriter = IFileManager::Get().CreateFileWriter(*FileName);
		FDiagnosticTableWriterCSV StatWriter(DebugWriter);
		const TSparseArray<ShaderCompilerStats>& PlatformStats = GetShaderCompilerStats();
		StatWriter.AddColumn(TEXT("Name"));
		StatWriter.AddColumn(TEXT("Platform"));
		StatWriter.AddColumn(TEXT("Compiles"));
		StatWriter.AddColumn(TEXT("CompilesDouble"));
		StatWriter.AddColumn(TEXT("Uses"));
		StatWriter.AddColumn(TEXT("UsesDouble"));
		StatWriter.AddColumn(TEXT("PermutationString"));
		StatWriter.CycleRow();


		for (int32 Platform = 0; Platform < PlatformStats.GetMaxIndex(); ++Platform)
		{
			if (PlatformStats.IsValidIndex(Platform))
			{
				const ShaderCompilerStats& Stats = PlatformStats[Platform];
				for (const auto& Pair : Stats)
				{
					const FString& Path = Pair.Key;
					const FShaderCompilerStats::FShaderStats& SingleStats = Pair.Value;
					for (const FShaderCompilerStats::FShaderCompilerSinglePermutationStat& Stat : SingleStats.PermutationCompilations)
					{
						StatWriter.AddColumn(*Path);
						StatWriter.AddColumn(TEXT("%u"), Platform);
						StatWriter.AddColumn(TEXT("%u"), Stat.Compiled);
						StatWriter.AddColumn(TEXT("%u"), Stat.CompiledDouble);
						StatWriter.AddColumn(TEXT("%u"), Stat.Cooked);
						StatWriter.AddColumn(TEXT("%u"), Stat.CookedDouble);
						StatWriter.AddColumn(TEXT("%s"), *Stat.PermutationString);
						StatWriter.CycleRow();
					}
				}

			}
		}
	}
#endif // ALLOW_DEBUG_FILES
}
void FShaderCompilerStats::RegisterCookedShaders(uint32 NumCooked, float CompileTime, EShaderPlatform Platform, const FString MaterialPath, FString PermutationString)
{
	FScopeLock Lock(&CompileStatsLock);
	if(!CompileStats.IsValidIndex(Platform))
	{
		ShaderCompilerStats Stats;
		CompileStats.Insert(Platform, Stats);
	}

	FShaderCompilerStats::FShaderStats& Stats = CompileStats[Platform].FindOrAdd(MaterialPath);
	Stats.CompileTime += CompileTime;
	bool bFound = false;
	for (FShaderCompilerSinglePermutationStat& Stat : Stats.PermutationCompilations)
	{
		if (PermutationString == Stat.PermutationString)
		{
			bFound = true;
			if (Stat.Cooked != 0)
			{
				Stat.CookedDouble += NumCooked;
				Stats.CookedDouble += NumCooked;
			}
			else
			{
				Stat.Cooked = NumCooked;
				Stats.Cooked += NumCooked;
			}
		}
	}
	if(!bFound)
	{
		Stats.Cooked += NumCooked;
	}
	if (!bFound)
	{
		Stats.PermutationCompilations.Emplace(PermutationString, 0, NumCooked);
	}
}

void FShaderCompilerStats::RegisterCompiledShaders(uint32 NumCompiled, EShaderPlatform Platform, const FString MaterialPath, FString PermutationString)
{
	FScopeLock Lock(&CompileStatsLock);
	if (!CompileStats.IsValidIndex(Platform))
	{
		ShaderCompilerStats Stats;
		CompileStats.Insert(Platform, Stats);
	}
	FShaderCompilerStats::FShaderStats& Stats = CompileStats[Platform].FindOrAdd(MaterialPath);

	bool bFound = false;
	for (FShaderCompilerSinglePermutationStat& Stat : Stats.PermutationCompilations)
	{
		if (PermutationString == Stat.PermutationString)
		{
			bFound = true;
			if (Stat.Compiled != 0)
			{
				Stat.CompiledDouble += NumCompiled;
				Stats.CompiledDouble += NumCompiled;
			}
			else
			{
				Stat.Compiled = NumCompiled;
				Stats.Compiled += NumCompiled;
			}
		}
	}
	if(!bFound)
	{
		Stats.Compiled += NumCompiled;
	}


	if (!bFound)
	{
		Stats.PermutationCompilations.Emplace(PermutationString, NumCompiled, 0);
	}
}

FShaderCompilingManager* GShaderCompilingManager = NULL;

bool FShaderCompilingManager::AllTargetPlatformSupportsRemoteShaderCompiling()
{
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();	
	if (!TPM)
	{
		return false;
	}
	
	const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();
	for (int32 Index = 0; Index < Platforms.Num(); Index++)
	{
		if (!Platforms[Index]->CanSupportRemoteShaderCompile())
		{
			return false;
		}
	}
	
	return true;
}

IDistributedBuildController* FShaderCompilingManager::FindRemoteCompilerController() const
{
	TArray<IDistributedBuildController*> AvailableControllers = IModularFeatures::Get().GetModularFeatureImplementations<IDistributedBuildController>(IDistributedBuildController::GetModularFeatureType());
	for (IDistributedBuildController* Controller : AvailableControllers)
	{
		if (Controller != nullptr && Controller->IsSupported())
		{
			Controller->InitializeController();
			return Controller;
		}
	}
	return nullptr;
}

FShaderCompilingManager::FShaderCompilingManager() :
	bCompilingDuringGame(false),
	NumExternalJobs(0),
	NumSingleThreadedRunsBeforeRetry(GSingleThreadedRunsIdle),
#if PLATFORM_MAC
	ShaderCompileWorkerName(FPaths::EngineDir() / TEXT("Binaries/Mac/ShaderCompileWorker")),
#elif PLATFORM_LINUX
	ShaderCompileWorkerName(FPaths::EngineDir() / TEXT("Binaries/Linux/ShaderCompileWorker")),
#else
	ShaderCompileWorkerName(FPaths::EngineDir() / TEXT("Binaries/Win64/ShaderCompileWorker.exe")),
#endif
	SuppressedShaderPlatforms(0),
	bNoShaderCompilation(false)
{
	bool bForceUseSCWMemoryPressureLimits = false;
	
	BuildDistributionController = nullptr;
	
	WorkersBusyTime = 0;

	// Threads must use absolute paths on Windows in case the current directory is changed on another thread!
	ShaderCompileWorkerName = FPaths::ConvertRelativePathToFull(ShaderCompileWorkerName);

	// Read values from the engine ini
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bAllowCompilingThroughWorkers"), bAllowCompilingThroughWorkers, GEngineIni ));
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bAllowAsynchronousShaderCompiling"), bAllowAsynchronousShaderCompiling, GEngineIni ));

	// override the use of workers, can be helpful for debugging shader compiler code
	static const IConsoleVariable* CVarAllowCompilingThroughWorkers = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.AllowCompilingThroughWorkers"), false);
	if (!FPlatformProcess::SupportsMultithreading() || FParse::Param(FCommandLine::Get(), TEXT("noshaderworker")) || (CVarAllowCompilingThroughWorkers && CVarAllowCompilingThroughWorkers->GetInt() == 0))
	{
		bAllowCompilingThroughWorkers = false;
	}

	if (!FPlatformProcess::SupportsMultithreading())
	{
		bAllowAsynchronousShaderCompiling = false;
	}

	int32 NumUnusedShaderCompilingThreads;
	verify(GConfig->GetInt( TEXT("DevOptions.Shaders"), TEXT("NumUnusedShaderCompilingThreads"), NumUnusedShaderCompilingThreads, GEngineIni ));

	int32 NumUnusedShaderCompilingThreadsDuringGame;
	verify(GConfig->GetInt( TEXT("DevOptions.Shaders"), TEXT("NumUnusedShaderCompilingThreadsDuringGame"), NumUnusedShaderCompilingThreadsDuringGame, GEngineIni ));

	// Use all the cores on the build machines.
	if (GForceAllCoresForShaderCompiling != 0)
	{
		NumUnusedShaderCompilingThreads = 0;
	}

	verify(GConfig->GetInt( TEXT("DevOptions.Shaders"), TEXT("MaxShaderJobBatchSize"), MaxShaderJobBatchSize, GEngineIni ));
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bPromptToRetryFailedShaderCompiles"), bPromptToRetryFailedShaderCompiles, GEngineIni ));
	verify(GConfig->GetBool( TEXT("DevOptions.Shaders"), TEXT("bLogJobCompletionTimes"), bLogJobCompletionTimes, GEngineIni ));
	GConfig->GetFloat(TEXT("DevOptions.Shaders"), TEXT("WorkerTimeToLive"), GRegularWorkerTimeToLive, GEngineIni);
	GConfig->GetFloat(TEXT("DevOptions.Shaders"), TEXT("BuildWorkerTimeToLive"), GBuildWorkerTimeToLive, GEngineIni);
	GConfig->GetBool(TEXT("DevOptions.Shaders"), TEXT("bForceUseSCWMemoryPressureLimits"), bForceUseSCWMemoryPressureLimits, GEngineIni);

	GRetryShaderCompilation = bPromptToRetryFailedShaderCompiles;

	verify(GConfig->GetFloat( TEXT("DevOptions.Shaders"), TEXT("ProcessGameThreadTargetTime"), ProcessGameThreadTargetTime, GEngineIni ));

#if UE_BUILD_DEBUG
	// Increase budget for processing results in debug or else it takes forever to finish due to poor framerate
	ProcessGameThreadTargetTime *= 3;
#endif

	// Get the current process Id, this will be used by the worker app to shut down when it's parent is no longer running.
	ProcessId = FPlatformProcess::GetCurrentProcessId();

	// Use a working directory unique to this game, process and thread so that it will not conflict 
	// With processes from other games, processes from the same game or threads in this same process.
	// Use IFileManager to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
	//ShaderBaseWorkingDirectory = FPlatformProcess::ShaderWorkingDir() / FString::FromInt(ProcessId) + TEXT("/");

	{
		FGuid Guid;
		Guid = FGuid::NewGuid();
		FString LegacyShaderWorkingDirectory = FPaths::ProjectIntermediateDir() / TEXT("Shaders/WorkingDirectory/")  / FString::FromInt(ProcessId) + TEXT("/");
		ShaderBaseWorkingDirectory = FPaths::ShaderWorkingDir() / *Guid.ToString(EGuidFormats::Digits) + TEXT("/");
		UE_LOG(LogShaderCompilers, Log, TEXT("Guid format shader working directory is %d characters bigger than the processId version (%s)."), ShaderBaseWorkingDirectory.Len() - LegacyShaderWorkingDirectory.Len(), *LegacyShaderWorkingDirectory );
	}

	if (!IFileManager::Get().DeleteDirectory(*ShaderBaseWorkingDirectory, false, true))
	{
		UE_LOG(LogShaderCompilers, Fatal, TEXT("Could not delete the shader compiler working directory '%s'."), *ShaderBaseWorkingDirectory);
	}
	else
	{
		UE_LOG(LogShaderCompilers, Log, TEXT("Cleaned the shader compiler working directory '%s'."), *ShaderBaseWorkingDirectory);
	}
	FString AbsoluteBaseDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*ShaderBaseWorkingDirectory);
	FPaths::NormalizeDirectoryName(AbsoluteBaseDirectory);
	AbsoluteShaderBaseWorkingDirectory = AbsoluteBaseDirectory + TEXT("/");

	FString AbsoluteDebugInfoDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(FPaths::ProjectSavedDir() / TEXT("ShaderDebugInfo")));
	const FString OverrideShaderDebugDir = CVarShaderOverrideDebugDir.GetValueOnAnyThread();
	if (!OverrideShaderDebugDir.IsEmpty())
	{
		AbsoluteDebugInfoDirectory = OverrideShaderDebugDir;
	}
	FPaths::NormalizeDirectoryName(AbsoluteDebugInfoDirectory);
	AbsoluteShaderDebugInfoDirectory = AbsoluteDebugInfoDirectory;

	const int32 NumVirtualCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

	NumShaderCompilingThreads = (bAllowCompilingThroughWorkers && NumVirtualCores > NumUnusedShaderCompilingThreads) ? (NumVirtualCores - NumUnusedShaderCompilingThreads) : 1;

	// Make sure there's at least one worker allowed to be active when compiling during the game
	NumShaderCompilingThreadsDuringGame = (bAllowCompilingThroughWorkers && NumVirtualCores > NumUnusedShaderCompilingThreadsDuringGame) ? (NumVirtualCores - NumUnusedShaderCompilingThreadsDuringGame) : 1;

	// On machines with few cores, each core will have a massive impact on compile time, so we prioritize compile latency over editor performance during the build
	if (NumVirtualCores <= 4)
	{
		NumShaderCompilingThreads = NumVirtualCores - 1;
		NumShaderCompilingThreadsDuringGame = NumVirtualCores - 1;
	}
#if PLATFORM_DESKTOP
	else if (GIsBuildMachine || bForceUseSCWMemoryPressureLimits)
	{
		// Cooker ends up running OOM so use a simple heuristic based on some INI values
		float CookerMemoryUsedInGB = 0.0f;
		float MemoryToLeaveForTheOSInGB = 0.0f;
		float MemoryUsedPerSCWProcessInGB = 0.0f;
		bool bFoundEntries = true;
		bFoundEntries = bFoundEntries && GConfig->GetFloat(TEXT("DevOptions.Shaders"), TEXT("CookerMemoryUsedInGB"), CookerMemoryUsedInGB, GEngineIni);
		bFoundEntries = bFoundEntries && GConfig->GetFloat(TEXT("DevOptions.Shaders"), TEXT("MemoryToLeaveForTheOSInGB"), MemoryToLeaveForTheOSInGB, GEngineIni);
		bFoundEntries = bFoundEntries && GConfig->GetFloat(TEXT("DevOptions.Shaders"), TEXT("MemoryUsedPerSCWProcessInGB"), MemoryUsedPerSCWProcessInGB, GEngineIni);
		if (bFoundEntries)
		{
			uint32 PhysicalGBRam = FPlatformMemory::GetPhysicalGBRam();
			float AvailableMemInGB = (float)PhysicalGBRam - CookerMemoryUsedInGB;
			if (AvailableMemInGB > 0.0f)
			{
				if (AvailableMemInGB > MemoryToLeaveForTheOSInGB)
				{
					AvailableMemInGB -= MemoryToLeaveForTheOSInGB;
				}
				else
				{
					UE_LOG(LogShaderCompilers, Warning, TEXT("Machine has %d GBs of RAM, cooker might take %f GBs, but not enough memory left for the OS! (Requested %f GBs for the OS)"), PhysicalGBRam, CookerMemoryUsedInGB, MemoryToLeaveForTheOSInGB);
				}
			}
			else
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("Machine has %d GBs of RAM, but cooker might take %f GBs!"), PhysicalGBRam, CookerMemoryUsedInGB);
			}
			if (MemoryUsedPerSCWProcessInGB > 0.0f)
			{
				float NumSCWs = AvailableMemInGB / MemoryUsedPerSCWProcessInGB;
				NumShaderCompilingThreads = FMath::RoundToInt(NumSCWs);

				bool bUseVirtualCores = true;
				GConfig->GetBool(TEXT("DevOptions.Shaders"), TEXT("bUseVirtualCores"), bUseVirtualCores, GEngineIni);
				uint32 MaxNumCoresToUse = bUseVirtualCores ? NumVirtualCores : FPlatformMisc::NumberOfCores();
				NumShaderCompilingThreads = FMath::Clamp<uint32>(NumShaderCompilingThreads, 1, MaxNumCoresToUse - 1);
				NumShaderCompilingThreadsDuringGame = FMath::Min<int32>(NumShaderCompilingThreads, NumShaderCompilingThreadsDuringGame);
			}
		}
		else if (bForceUseSCWMemoryPressureLimits)
		{
			UE_LOG(LogShaderCompilers, Warning, TEXT("bForceUseSCWMemoryPressureLimits was set but missing one or more prerequisite setting(s): CookerMemoryUsedInGB, MemoryToLeaveForTheOSInGB, MemoryUsedPerSCWProcessInGB.  Ignoring bForceUseSCWMemoryPressureLimits") );
		}

		if (GIsBuildMachine)
		{
			// force crashes on hung shader maps on build machines, to prevent builds running for days
			GCrashOnHungShaderMaps = 1;
		}
	}
#endif

	NumShaderCompilingThreads = FMath::Max<int32>(1, NumShaderCompilingThreads);
	NumShaderCompilingThreadsDuringGame = FMath::Max<int32>(1, NumShaderCompilingThreadsDuringGame);

	NumShaderCompilingThreadsDuringGame = FMath::Min<int32>(NumShaderCompilingThreadsDuringGame, NumShaderCompilingThreads);

	TUniquePtr<FShaderCompileThreadRunnableBase> RemoteCompileThread;
#if PLATFORM_WINDOWS
	const bool bCanUseRemoteCompiling = bAllowCompilingThroughWorkers && AllTargetPlatformSupportsRemoteShaderCompiling();
	BuildDistributionController = bCanUseRemoteCompiling ? FindRemoteCompilerController() : nullptr;
	
	if (BuildDistributionController)
	{
		UE_LOG(LogShaderCompilers, Display, TEXT("Using %s for Shader Compilation."), *BuildDistributionController->GetName());
		RemoteCompileThread = MakeUnique<FShaderCompileDistributedThreadRunnable_Interface>(this, *BuildDistributionController);
	}
	else if (bCanUseRemoteCompiling && FShaderCompileXGEThreadRunnable_XmlInterface::IsSupported())
	{
		UE_LOG(LogShaderCompilers, Display, TEXT("Using XGE Shader Compiler (XML Interface)."));
		RemoteCompileThread = MakeUnique<FShaderCompileXGEThreadRunnable_XmlInterface>(this);
	}
	else
#endif // PLATFORM_WINDOWS
#if PLATFORM_DESKTOP
	if (bAllowCompilingThroughWorkers && FShaderCompileFASTBuildThreadRunnable::IsSupported())
	{
		UE_LOG(LogShaderCompilers, Display, TEXT("Using FASTBuild Shader Compiler."));
		RemoteCompileThread = MakeUnique<FShaderCompileFASTBuildThreadRunnable>(this);
	}
#endif // PLATFORM_DESKTOP

	GConfig->SetBool(TEXT("/Script/UnrealEd.UnrealEdOptions"), TEXT("UsingXGE"), RemoteCompileThread.IsValid(), GEditorIni);

	TUniquePtr<FShaderCompileThreadRunnableBase> LocalThread = MakeUnique<FShaderCompileThreadRunnable>(this);
	if (RemoteCompileThread)
	{
		// Keep high priority jobs on the local machine, to avoid XGE latency
		RemoteCompileThread->SetPriorityRange(EShaderCompileJobPriority::Low, EShaderCompileJobPriority::High);
		LocalThread->SetPriorityRange(EShaderCompileJobPriority::Normal, EShaderCompileJobPriority::ForceLocal);
		Threads.Add(MoveTemp(RemoteCompileThread));
	}
	else
	{
		UE_LOG(LogShaderCompilers, Display, TEXT("Using Local Shader Compiler."));

		if (GIsBuildMachine)
		{
			int32 MinSCWsToSpawnBeforeWarning = 8; // optional, default to 8
			GConfig->GetInt(TEXT("DevOptions.Shaders"), TEXT("MinSCWsToSpawnBeforeWarning"), MinSCWsToSpawnBeforeWarning, GEngineIni);
			if (NumShaderCompilingThreads < static_cast<uint32>(MinSCWsToSpawnBeforeWarning))
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("Only %d SCWs will be spawned, which will result in longer shader compile times."), NumShaderCompilingThreads);
			}
		}
	}
	Threads.Add(MoveTemp(LocalThread));

	for (const auto& Thread : Threads)
	{
		Thread->StartThread();
	}
}

int32 FShaderCompilingManager::GetNumPendingJobs() const
{
	return AllJobs.GetNumPendingJobs();
}

int32 FShaderCompilingManager::GetNumOutstandingJobs() const
{
	return AllJobs.GetNumOutstandingJobs();
}

FShaderCompilingManager::EDumpShaderDebugInfo FShaderCompilingManager::GetDumpShaderDebugInfo() const
{
	if (GDumpShaderDebugInfo < EDumpShaderDebugInfo::Never || GDumpShaderDebugInfo > EDumpShaderDebugInfo::OnErrorOrWarning)
	{
		return EDumpShaderDebugInfo::Never;
	}

	return static_cast<FShaderCompilingManager::EDumpShaderDebugInfo>(GDumpShaderDebugInfo);
}

FString FShaderCompilingManager::CreateShaderDebugInfoPath(const FShaderCompilerInput& ShaderCompilerInput) const
{
	FString DumpDebugInfoPath = ShaderCompilerInput.DumpDebugInfoRootPath / ShaderCompilerInput.DebugGroupName + ShaderCompilerInput.DebugExtension;

	// Sanitize the name to be used as a path
	// List mostly comes from set of characters not allowed by windows in a path.  Just try to rename a file and type one of these for the list.
	DumpDebugInfoPath.ReplaceInline(TEXT("<"), TEXT("("));
	DumpDebugInfoPath.ReplaceInline(TEXT(">"), TEXT(")"));
	DumpDebugInfoPath.ReplaceInline(TEXT("::"), TEXT("=="));
	DumpDebugInfoPath.ReplaceInline(TEXT("|"), TEXT("_"));
	DumpDebugInfoPath.ReplaceInline(TEXT("*"), TEXT("-"));
	DumpDebugInfoPath.ReplaceInline(TEXT("?"), TEXT("!"));
	DumpDebugInfoPath.ReplaceInline(TEXT("\""), TEXT("\'"));

	if (!IFileManager::Get().DirectoryExists(*DumpDebugInfoPath))
	{
		verifyf(IFileManager::Get().MakeDirectory(*DumpDebugInfoPath, true), TEXT("Failed to create directory for shader debug info '%s'"), *DumpDebugInfoPath);
	}

	return DumpDebugInfoPath;
}

bool FShaderCompilingManager::ShouldRecompileToDumpShaderDebugInfo(const FShaderCompileJob& Job) const
{
	return ShouldRecompileToDumpShaderDebugInfo(Job.Input, Job.Output, Job.bSucceeded);
}

bool FShaderCompilingManager::ShouldRecompileToDumpShaderDebugInfo(const FShaderCompilerInput& Input, const FShaderCompilerOutput& Output, bool bSucceeded) const
{
	if (Input.DumpDebugInfoPath.IsEmpty())
	{
		const EDumpShaderDebugInfo DumpShaderDebugInfo = GetDumpShaderDebugInfo();

		if (DumpShaderDebugInfo == EDumpShaderDebugInfo::OnError)
		{
			return !bSucceeded;
		}
		else if (DumpShaderDebugInfo == EDumpShaderDebugInfo::OnErrorOrWarning)
		{
			return !bSucceeded || Output.Errors.Num() > 0;
		}
	}

	return false;
}

void FShaderCompilingManager::ReleaseJob(FShaderCommonCompileJobPtr& Job)
{
	ReleaseJob(Job.GetReference());
	Job.SafeRelease();
}

void FShaderCompilingManager::ReleaseJob(FShaderCommonCompileJob* Job)
{
	Job->bReleased = true;
	AllJobs.RemoveJob(Job);
}

void FShaderCompilingManager::SubmitJobs(TArray<FShaderCommonCompileJobPtr>& NewJobs, const FString MaterialBasePath, const FString PermutationString)
{
	check(!FPlatformProperties::RequiresCookedData());

	if (NewJobs.Num() == 0)
	{
		return;
	}

	check(GShaderCompilerStats);
	if (FShaderCompileJob* SingleJob = NewJobs[0]->GetSingleShaderJob()) //assume that all jobs are for the same platform
	{
		GShaderCompilerStats->RegisterCompiledShaders(NewJobs.Num(), SingleJob->Input.Target.GetPlatform(), MaterialBasePath, PermutationString);
	}
	else
	{
		GShaderCompilerStats->RegisterCompiledShaders(NewJobs.Num(), SP_NumPlatforms, MaterialBasePath, PermutationString);
	}

	{
		FScopeLock Lock(&CompileQueueSection);
		for (auto& Job : NewJobs)
		{
			FPendingShaderMapCompileResultsPtr& PendingShaderMap = ShaderMapJobs.FindOrAdd(Job->Id);
			if (!PendingShaderMap)
			{
				PendingShaderMap = new FPendingShaderMapCompileResults();
			}
			PendingShaderMap->NumPendingJobs.Increment();
			Job->PendingShaderMap = PendingShaderMap;
		}
	}

	AllJobs.SubmitJobs(NewJobs);
}

FShaderCompileJob* FShaderCompilingManager::PrepareShaderCompileJob(uint32 Id, const FShaderCompileJobKey& Key, EShaderCompileJobPriority Priority)
{
	return AllJobs.PrepareJob(Id, Key, Priority);
}

FShaderPipelineCompileJob* FShaderCompilingManager::PreparePipelineCompileJob(uint32 Id, const FShaderPipelineCompileJobKey& Key, EShaderCompileJobPriority Priority)
{
	return AllJobs.PrepareJob(Id, Key, Priority);
}

void FShaderCompilingManager::ProcessFinishedJob(FShaderCommonCompileJob* FinishedJob)
{
	AllJobs.ProcessFinishedJob(FinishedJob);
}

/** Launches the worker, returns the launched process handle. */
FProcHandle FShaderCompilingManager::LaunchWorker(const FString& WorkingDirectory, uint32 InProcessId, uint32 ThreadId, const FString& WorkerInputFile, const FString& WorkerOutputFile)
{
	// Setup the parameters that the worker application needs
	// Surround the working directory with double quotes because it may contain a space 
	// WorkingDirectory ends with a '\', so we have to insert another to meet the Windows commandline parsing rules 
	// http://msdn.microsoft.com/en-us/library/17w5ykft.aspx 
	// Use IFileManager to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
	FString WorkerAbsoluteDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*WorkingDirectory);
	FPaths::NormalizeDirectoryName(WorkerAbsoluteDirectory);
	FString WorkerParameters = FString(TEXT("\"")) + WorkerAbsoluteDirectory + TEXT("/\" ") + FString::FromInt(InProcessId) + TEXT(" ") + FString::FromInt(ThreadId) + TEXT(" ") + WorkerInputFile + TEXT(" ") + WorkerOutputFile;
	WorkerParameters += FString(TEXT(" -communicatethroughfile "));
	if ( GIsBuildMachine )
	{
		WorkerParameters += FString::Printf(TEXT(" -TimeToLive=%f -buildmachine"), GBuildWorkerTimeToLive);
	}
	else
	{
		WorkerParameters += FString::Printf(TEXT(" -TimeToLive=%f"), GRegularWorkerTimeToLive);
	}
	if (PLATFORM_LINUX) //-V560
	{
		// suppress log generation as much as possible
		WorkerParameters += FString(TEXT(" -logcmds=\"Global None\" "));

		if (UE_BUILD_DEBUG)
		{
			// when running a debug build under Linux, make SCW crash with core for easier debugging
			WorkerParameters += FString(TEXT(" -core "));
		}
	}
	WorkerParameters += FCommandLine::GetSubprocessCommandline();

	// Launch the worker process
	int32 PriorityModifier = -1; // below normal
	GConfig->GetInt(TEXT("DevOptions.Shaders"), TEXT("WorkerProcessPriority"), PriorityModifier, GEngineIni);

	if (DEBUG_SHADERCOMPILEWORKER)
	{
		// Note: Set breakpoint here and launch the ShaderCompileWorker with WorkerParameters a cmd-line
		const TCHAR* WorkerParametersText = *WorkerParameters;
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Launching shader compile worker w/ WorkerParameters\n\t%s\n"), WorkerParametersText);
		FProcHandle DummyHandle;
		return DummyHandle;
	}
	else
	{
#if UE_BUILD_DEBUG && PLATFORM_LINUX
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Launching shader compile worker:\n\t%s\n"), *WorkerParameters);
#endif
		// Disambiguate between SCW.exe missing vs other errors.
		static bool bFirstLaunch = true;
		uint32 WorkerId = 0;
		FProcHandle WorkerHandle = FPlatformProcess::CreateProc(*ShaderCompileWorkerName, *WorkerParameters, true, false, false, &WorkerId, PriorityModifier, NULL, NULL);
		if (WorkerHandle.IsValid())
		{
			// Process launched at least once successfully
			bFirstLaunch = false;
		}
		else
		{
			// If this doesn't error, the app will hang waiting for jobs that can never be completed
			if (bFirstLaunch)
			{
				// When using source builds users are likely to make a mistake of not building SCW (e.g. in particular on Linux, even though default makefile target builds it).
				// Make the engine exit gracefully with a helpful message instead of a crash.
				static bool bShowedMessageBox = false;
				if (!bShowedMessageBox && !IsRunningCommandlet() && !FApp::IsUnattended())
				{
					bShowedMessageBox = true;
					FText ErrorMessage = FText::Format(LOCTEXT("LaunchingShaderCompileWorkerFailed", "Unable to launch {0} - make sure you built ShaderCompileWorker."), FText::FromString(ShaderCompileWorkerName));
					FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMessage.ToString(),
												 *LOCTEXT("LaunchingShaderCompileWorkerFailedTitle", "Unable to launch ShaderCompileWorker.").ToString());
				}
				UE_LOG(LogShaderCompilers, Error, TEXT("Couldn't launch %s! Make sure you build ShaderCompileWorker."), *ShaderCompileWorkerName);
				// duplicate to printf() since threaded logs may not be always flushed
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Couldn't launch %s! Make sure you build ShaderCompileWorker.\n"), *ShaderCompileWorkerName);
				FPlatformMisc::RequestExitWithStatus(true, 1);
			}
			else
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Couldn't launch %s!"), *ShaderCompileWorkerName);
			}
		}

		return WorkerHandle;
	}
}

void FShaderCompilingManager::AddCompiledResults(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps, int32 ShaderMapIdx, const FShaderMapFinalizeResults& Results)
{
	// merge with the previous unprocessed jobs, if any
	if (FShaderMapCompileResults const* PrevResults = CompiledShaderMaps.Find(ShaderMapIdx))
	{
		FShaderMapFinalizeResults NewResults(Results);

		NewResults.bAllJobsSucceeded = NewResults.bAllJobsSucceeded && PrevResults->bAllJobsSucceeded;
		NewResults.bSkipResultProcessing = NewResults.bSkipResultProcessing || PrevResults->bSkipResultProcessing;
		NewResults.TimeStarted = FMath::Min(NewResults.TimeStarted, PrevResults->TimeStarted);
		NewResults.bIsHung = NewResults.bIsHung || PrevResults->bIsHung;
		NewResults.FinishedJobs.Append(PrevResults->FinishedJobs);

		CompiledShaderMaps.Add(ShaderMapIdx, NewResults);
	}
	else
	{
		CompiledShaderMaps.Add(ShaderMapIdx, Results);
	}
}

/** Flushes all pending jobs for the given shader maps. */
void FShaderCompilingManager::BlockOnShaderMapCompletion(const TArray<int32>& ShaderMapIdsToFinishCompiling, TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps)
{
	COOK_STAT(FScopedDurationTimer BlockingTimer(ShaderCompilerCookStats::BlockingTimeSec));
	if (bAllowAsynchronousShaderCompiling)
	{
		// Calculate how many shader jobs there are total to provide the slow task with the correct amount of work.
		int NumJobs = 0;
		{
			FScopeLock Lock(&CompileQueueSection);
			for (int32 ShaderMapIndex = 0; ShaderMapIndex < ShaderMapIdsToFinishCompiling.Num(); ShaderMapIndex++)
			{
				FPendingShaderMapCompileResultsPtr* ResultsPtr = ShaderMapJobs.Find(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);
				if (ResultsPtr)
				{
					FShaderMapCompileResults* Results = *ResultsPtr;
					NumJobs += Results->NumPendingJobs.GetValue();
				}
			}
		}

		FScopedSlowTask SlowTask(NumJobs, FText::Format(LOCTEXT("BlockOnShaderMapCompletion", "Compiling Shaders ({0})"), NumJobs), GIsEditor && !IsRunningCommandlet());

		int32 NumPendingJobs = 0;
		// Keep track of previous number of pending jobs so we can update the slow task with the amount of work done.
		int32 NumPreviousPendingJobs = NumJobs;
		int32 LogCounter = 0;
		do 
		{
			for (const auto& Thread : Threads)
			{
				Thread->CheckHealth();
			}
			NumPendingJobs = 0;
			{
				// Lock CompileQueueSection so we can access the input and output queues
				FScopeLock Lock(&CompileQueueSection);

				for (int32 ShaderMapIndex = 0; ShaderMapIndex < ShaderMapIdsToFinishCompiling.Num(); ShaderMapIndex++)
				{
					FPendingShaderMapCompileResultsPtr* ResultsPtr = ShaderMapJobs.Find(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);
					if (ResultsPtr)
					{
						FShaderMapCompileResults* Results = *ResultsPtr;

						if (Results->NumPendingJobs.GetValue() == 0)
						{
							if (Results->FinishedJobs.Num() > 0)
							{
								AddCompiledResults(CompiledShaderMaps, ShaderMapIdsToFinishCompiling[ShaderMapIndex], *Results);
							}
							ShaderMapJobs.Remove(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);
						}
						else
						{
							Results->CheckIfHung();
							NumPendingJobs += Results->NumPendingJobs.GetValue();
						}
					}
				}
			}

			if (NumPendingJobs > 0)
			{
				const float SleepTime =.01f;
				
				// We need to manually tick the Distributed build controller while the game thread is blocked
				// otherwise we can get stuck in a infinite loop waiting for jobs that never will be done
				// because for example, some controllers depend on the HTTP module which needs to be ticked in the main thread
				if (BuildDistributionController && IsInGameThread())
				{
					BuildDistributionController->Tick(SleepTime);
				}

				// Progress the slow task with how many jobs we've completed since last tick.  Update the slow task message with the current number of pending jobs
				// we are waiting on.
				const int32 CompletedJobsSinceLastTick = NumPreviousPendingJobs - NumPendingJobs;
				SlowTask.EnterProgressFrame(CompletedJobsSinceLastTick, FText::Format(LOCTEXT("BlockOnShaderMapCompletion", "Compiling Shaders ({0})"), NumPendingJobs));
				NumPreviousPendingJobs = NumPendingJobs;
				
				// Yield CPU time while waiting
				FPlatformProcess::Sleep(SleepTime);

				// Flush threaded logs around every 500ms or so based on Sleep of 0.01f seconds above
				if (++LogCounter > 50)
				{
					LogCounter = 0;
					GLog->FlushThreadedLogs();
				}
			}
		} 
		while (NumPendingJobs > 0);
	}
	else
	{
		int32 NumActiveWorkers = 0;
		do 
		{
			for (const auto& Thread : Threads)
			{
				NumActiveWorkers = Thread->CompilingLoop();
			}
		} 
		while (NumActiveWorkers > 0);

		check(AllJobs.GetNumPendingJobs() == 0);

		for (int32 ShaderMapIndex = 0; ShaderMapIndex < ShaderMapIdsToFinishCompiling.Num(); ShaderMapIndex++)
		{
			const FPendingShaderMapCompileResultsPtr* ResultsPtr = ShaderMapJobs.Find(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);

			if (ResultsPtr)
			{
				const FShaderMapCompileResults* Results = *ResultsPtr;
				check(Results->NumPendingJobs.GetValue() == 0);
				check(Results->FinishedJobs.Num() > 0);

				AddCompiledResults(CompiledShaderMaps, ShaderMapIdsToFinishCompiling[ShaderMapIndex], *Results);
				ShaderMapJobs.Remove(ShaderMapIdsToFinishCompiling[ShaderMapIndex]);
			}
		}
	}
}

void FShaderCompilingManager::BlockOnAllShaderMapCompletion(TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps)
{
	COOK_STAT(FScopedDurationTimer BlockingTimer(ShaderCompilerCookStats::BlockingTimeSec));
	if (bAllowAsynchronousShaderCompiling)
	{
		// Calculate how many shader jobs there are total to provide the slow task with the correct amount of work.
		int NumJobs = 0;
		{
			FScopeLock Lock(&CompileQueueSection);
			for (TMap<int32, FPendingShaderMapCompileResultsPtr>::TIterator It(ShaderMapJobs); It; ++It)
			{
				FShaderMapCompileResults* Results = It.Value();
				NumJobs += Results->NumPendingJobs.GetValue();
			}
		}

		FScopedSlowTask SlowTask(NumJobs, FText::Format(LOCTEXT("BlockOnAllShaderMapCompletion", "Compiling Shaders ({0})"), NumJobs), GIsEditor && !IsRunningCommandlet());

		int32 NumPendingJobs = 0;
		// Keep track of previous number of pending jobs so we can update the slow task with the amount of work done.
		int32 NumPreviousPendingJobs = NumJobs;

		do 
		{
			for (const auto& Thread : Threads)
			{
				Thread->CheckHealth();
			}
			NumPendingJobs = 0;
			{
				// Lock CompileQueueSection so we can access the input and output queues
				FScopeLock Lock(&CompileQueueSection);

				int32 ShaderMapIdx = 0;
				for (TMap<int32, FPendingShaderMapCompileResultsPtr>::TIterator It(ShaderMapJobs); It; ++It)
				{
					FShaderMapCompileResults* Results = It.Value();

					if (Results->NumPendingJobs.GetValue() == 0)
					{
						AddCompiledResults(CompiledShaderMaps, It.Key(), *Results);
						It.RemoveCurrent();
					}
					else
					{
						Results->CheckIfHung();
						NumPendingJobs += Results->NumPendingJobs.GetValue();
					}
				}
			}

			if (NumPendingJobs > 0)
			{
				const float SleepTime =.01f;
				
				// We need to manually tick the Distributed build controller while the game thread is blocked
				// otherwise we can get stuck in a infinite loop waiting for jobs that never will be done
				// because for example, some controllers depend on the HTTP module which needs to be ticked in the main thread
				if (BuildDistributionController && IsInGameThread())
				{
					BuildDistributionController->Tick(SleepTime);
				}

				// Progress the slow task with how many jobs we've completed since last tick.  Update the slow task message with the current number of pending jobs
				// we are waiting on.
				const int32 CompletedJobsSinceLastTick = NumPreviousPendingJobs - NumPendingJobs;
				SlowTask.EnterProgressFrame(CompletedJobsSinceLastTick, FText::Format(LOCTEXT("BlockOnAllShaderMapCompletion", "Compiling Shaders ({0})"), NumPendingJobs));
				NumPreviousPendingJobs = NumPendingJobs;
				
				// Yield CPU time while waiting
				FPlatformProcess::Sleep(SleepTime);
			}
		} 
		while (NumPendingJobs > 0);
	}
	else
	{
		int32 NumActiveWorkers = 0;
		do 
		{
			for (const auto& Thread : Threads)
			{
				NumActiveWorkers = Thread->CompilingLoop();
			}

			for (TMap<int32, FPendingShaderMapCompileResultsPtr>::TIterator It(ShaderMapJobs); It; ++It)
			{
				FShaderMapCompileResults* Results = It.Value();
				Results->CheckIfHung();
			}
		} 
		while (NumActiveWorkers > 0);

		check(AllJobs.GetNumPendingJobs() == 0);

		for (TMap<int32, FPendingShaderMapCompileResultsPtr>::TIterator It(ShaderMapJobs); It; ++It)
		{
			const FShaderMapCompileResults* Results = It.Value();
			check(Results->NumPendingJobs.GetValue()== 0);

			AddCompiledResults(CompiledShaderMaps, It.Key(), *Results);
			It.RemoveCurrent();
		}
	}
}

void FShaderCompilingManager::ProcessCompiledShaderMaps(
	TMap<int32, FShaderMapFinalizeResults>& CompiledShaderMaps, 
	float TimeBudget)
{
#if WITH_EDITOR
	TMap<TRefCountPtr<FMaterial>, TRefCountPtr<FMaterialShaderMap>> MaterialsToUpdate;
	TArray<TRefCountPtr<FMaterial>> MaterialsToReleaseCompilingId;

	// Process compiled shader maps in FIFO order, in case a shader map has been enqueued multiple times,
	// Which can happen if a material is edited while a background compile is going on
	for (TMap<int32, FShaderMapFinalizeResults>::TIterator ProcessIt(CompiledShaderMaps); ProcessIt; ++ProcessIt)
	{
		const uint32 CompilingId = ProcessIt.Key();
		FShaderMapFinalizeResults& CompileResults = ProcessIt.Value();
		if (CompileResults.bSkipResultProcessing)
		{
			ProcessIt.RemoveCurrent();
			continue;
		}

		TRefCountPtr<FMaterialShaderMap> CompilingShaderMap = FMaterialShaderMap::FindCompilingShaderMap(CompilingId);

		if (CompilingShaderMap)
		{
			TArray<TRefCountPtr<FMaterial>>& MaterialDependencies = CompilingShaderMap->CompilingMaterialDependencies;

			TArray<FString> Errors;
			TArray<FShaderCommonCompileJobPtr>& ResultArray = CompileResults.FinishedJobs;

			bool bSuccess = true;
			for (int32 JobIndex = 0; JobIndex < ResultArray.Num(); JobIndex++)
			{
				FShaderCommonCompileJob& CurrentJob = *ResultArray[JobIndex];

				auto* SingleJob = CurrentJob.GetSingleShaderJob();
				if (SingleJob)
				{
					const bool bCheckSucceeded = CheckSingleJob(SingleJob, Errors);
					bSuccess = bCheckSucceeded && bSuccess;
				}
				else
				{
					auto* PipelineJob = CurrentJob.GetShaderPipelineJob();
					for (int32 Index = 0; Index < PipelineJob->StageJobs.Num(); ++Index)
					{
						const bool bCheckSucceeded = CheckSingleJob(PipelineJob->StageJobs[Index], Errors);
						bSuccess = PipelineJob->StageJobs[Index]->bSucceeded && bCheckSucceeded && bSuccess;
					}
				}
			}

			FMaterialShaderMap* ShaderMapToUseForRendering = nullptr;
			if (bSuccess)
			{
				int32 JobIndex = 0;
				if (ResultArray.Num() > 0)
				{
					CompilingShaderMap->ProcessCompilationResults(ResultArray, JobIndex, TimeBudget);
					{
						FScopeLock Lock(&CompileQueueSection);
						for (int32 i = 0; i < JobIndex; ++i)
						{
							ReleaseJob(ResultArray[i]);
						}
					}
					ResultArray.RemoveAt(0, JobIndex);
				}

				// Make a clone of the compiling shader map to use for rendering
				// This will allow rendering to proceed with the clone, while async compilation continues to potentially update the compiling shader map
				ShaderMapToUseForRendering = CompilingShaderMap->AcquireFinalizedClone();
			}

			if (!bSuccess || ResultArray.Num() == 0)
			{
				ProcessIt.RemoveCurrent();
			}

#if DEBUG_INFINITESHADERCOMPILE
			UE_LOG(LogTemp, Display, TEXT("Finished compile of shader map 0x%08X%08X"), (int)((int64)(ShaderMap.GetReference()) >> 32), (int)((int64)(ShaderMap.GetReference())));
#endif
			int32 NumIncompleteMaterials = 0;
			int32 MaterialIndex = 0;
			while (MaterialIndex < MaterialDependencies.Num())
			{
				FMaterial* Material = MaterialDependencies[MaterialIndex];
				check(Material->GetGameThreadCompilingShaderMapId() == CompilingShaderMap->GetCompilingId());

#if DEBUG_INFINITESHADERCOMPILE
				UE_LOG(LogTemp, Display, TEXT("Shader map %s complete, GameThreadShaderMap 0x%08X%08X, marking material %s as finished"), *ShaderMap->GetFriendlyName(), (int)((int64)(ShaderMap.GetReference()) >> 32), (int)((int64)(ShaderMap.GetReference())), *Material->GetFriendlyName());
				UE_LOG(LogTemp, Display, TEXT("Marking material as finished 0x%08X%08X"), (int)((int64)(Material) >> 32), (int)((int64)(Material)));
#endif
				//Material->RemoveOutstandingCompileId(ShaderMap->CompilingId);

				bool bReleaseCompilingId = false;

				// Only process results that still match the ID which requested a compile
				// This avoids applying shadermaps which are out of date and a newer one is in the async compiling pipeline
				if (Material->GetMaterialId() != CompilingShaderMap->GetShaderMapId().BaseMaterialId)
				{
					bReleaseCompilingId = true;
				}
				else if (bSuccess)
				{
					MaterialsToUpdate.Add(Material, ShaderMapToUseForRendering);
					if (ShaderMapToUseForRendering->IsComplete(Material, true))
					{
						bReleaseCompilingId = true;
					}
					else
					{
						++NumIncompleteMaterials;
					}

					if (GShowShaderWarnings && Errors.Num() > 0)
					{
						UE_LOG(LogShaderCompilers, Warning, TEXT("Warnings while compiling Material %s for platform %s:"),
							*Material->GetDebugName(),
							*LegacyShaderPlatformToShaderFormat(ShaderMapToUseForRendering->GetShaderPlatform()).ToString());
						for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
						{
							UE_LOG(LogShaders, Warning, TEXT("	%s"), *Errors[ErrorIndex]);
						}
					}
				}
				else
				{
					bReleaseCompilingId = true;
					// Propagate error messages
					Material->CompileErrors = Errors;

					MaterialsToUpdate.Add(Material, nullptr);

					if (Material->IsDefaultMaterial())
					{
						// Log the errors unsuppressed before the fatal error, so it's always obvious from the log what the compile error was
						for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
						{
							UE_LOG(LogShaderCompilers, Warning, TEXT("	%s"), *Errors[ErrorIndex]);
						}

						// Assert if a default material could not be compiled, since there will be nothing for other failed materials to fall back on.
						UE_LOG(LogShaderCompilers, Fatal, TEXT("Failed to compile default material %s!"), *Material->GetBaseMaterialPathName());
					}

					UE_LOG(LogShaderCompilers, Warning, TEXT("Failed to compile Material %s for platform %s, Default Material will be used in game."),
						*Material->GetDebugName(), *LegacyShaderPlatformToShaderFormat(CompilingShaderMap->GetShaderPlatform()).ToString());

					for (int32 ErrorIndex = 0; ErrorIndex < Errors.Num(); ErrorIndex++)
					{
						FString ErrorMessage = Errors[ErrorIndex];
						// Work around build machine string matching heuristics that will cause a cook to fail
						ErrorMessage.ReplaceInline(TEXT("error "), TEXT("err0r "), ESearchCase::CaseSensitive);
						UE_LOG(LogShaderCompilers, Display, TEXT("%s"), *ErrorMessage);
					}
				}

				if (bReleaseCompilingId)
				{
					check(Material->GameThreadCompilingShaderMapId != 0u);
					Material->GameThreadCompilingShaderMapId = 0u;
					MaterialDependencies.RemoveAt(MaterialIndex);
					MaterialsToReleaseCompilingId.Add(Material);
				}
				else
				{
					++MaterialIndex;
				}
			}

			if (NumIncompleteMaterials == 0)
			{
				CompilingShaderMap->bCompiledSuccessfully = bSuccess;
				CompilingShaderMap->bCompilationFinalized = true;
				if (bSuccess)
				{
					check(ShaderMapToUseForRendering);
					ShaderMapToUseForRendering->bCompiledSuccessfully = true;
					ShaderMapToUseForRendering->bCompilationFinalized = true;
					if (ShaderMapToUseForRendering->bIsPersistent)
					{
						ShaderMapToUseForRendering->SaveToDerivedDataCache(CompilingShaderMap->PendingCompilerEnvironment->TargetPlatform);
					}
				}

				CompilingShaderMap->ReleaseCompilingId();
			}

			if (TimeBudget < 0)
			{
				break;
			}
		}
		else if (CompilingId == GlobalShaderMapId)
		{
			ProcessCompiledGlobalShaders(CompileResults.FinishedJobs);
			{
				FScopeLock Lock(&CompileQueueSection);
				for (auto& Job : CompileResults.FinishedJobs)
				{
					ReleaseJob(Job);
				}
			}
			ProcessIt.RemoveCurrent();
		}
		else
		{
			// ShaderMap was removed from compiling list or is being used by another type of shader map which is maintaining a reference
			// to the results, either way the job can be released
			{
				FScopeLock Lock(&CompileQueueSection);
				for (auto& Job : CompileResults.FinishedJobs)
				{
					ReleaseJob(Job);
				}
			}
			ProcessIt.RemoveCurrent();
		}
	}

	if (MaterialsToReleaseCompilingId.Num() > 0)
	{
		ENQUEUE_RENDER_COMMAND(ReleaseCompilingShaderMapIds)([MaterialsToReleaseCompilingId = MoveTemp(MaterialsToReleaseCompilingId)](FRHICommandListImmediate& RHICmdList)
		{
			for (FMaterial* Material : MaterialsToReleaseCompilingId)
			{
				check(Material->RenderingThreadCompilingShaderMapId != 0u);
				Material->RenderingThreadCompilingShaderMapId = 0u;
				Material->RenderingThreadPendingCompilerEnvironment.SafeRelease();
			}
		});
	}

	if (MaterialsToUpdate.Num() > 0)
	{
		FMaterial::SetShaderMapsOnMaterialResources(MaterialsToUpdate);

		for (const auto& It : MaterialsToUpdate)
		{
			It.Key->NotifyCompilationFinished();
		}

		if (FApp::CanEverRender())
		{
			PropagateMaterialChangesToPrimitives(MaterialsToUpdate);

			FEditorSupportDelegates::RedrawAllViewports.Broadcast();
		}
	}

	AllJobs.LogCachingStats();
#endif // WITH_EDITOR
}

void FShaderCompilingManager::PropagateMaterialChangesToPrimitives(const TMap<TRefCountPtr<FMaterial>, TRefCountPtr<FMaterialShaderMap>>& MaterialsToUpdate)
{
	TArray<UMaterialInterface*> UsedMaterials;
	TIndirectArray<FComponentRecreateRenderStateContext> ComponentContexts;

	for (TObjectIterator<UPrimitiveComponent> PrimitiveIt; PrimitiveIt; ++PrimitiveIt)
	{
		UPrimitiveComponent* PrimitiveComponent = *PrimitiveIt;

		if (PrimitiveComponent->IsRenderStateCreated())
		{
			UsedMaterials.Reset();
			bool bPrimitiveIsDependentOnMaterial = false;

			// Note: relying on GetUsedMaterials to be accurate, or else we won't propagate to the right primitives and the renderer will crash later
			// FPrimitiveSceneProxy::VerifyUsedMaterial is used to make sure that all materials used for rendering are reported in GetUsedMaterials
			PrimitiveComponent->GetUsedMaterials(UsedMaterials);

			if (UsedMaterials.Num() > 0)
			{
				for (TMap<TRefCountPtr<FMaterial>, TRefCountPtr<FMaterialShaderMap>>::TConstIterator MaterialIt(MaterialsToUpdate); MaterialIt; ++MaterialIt)
				{
					FMaterial* UpdatedMaterial = MaterialIt.Key();
					UMaterialInterface* UpdatedMaterialInterface = UpdatedMaterial->GetMaterialInterface();
						
					if (UpdatedMaterialInterface)
					{
						for (int32 MaterialIndex = 0; MaterialIndex < UsedMaterials.Num(); MaterialIndex++)
						{
							UMaterialInterface* TestMaterial = UsedMaterials[MaterialIndex];

							if (TestMaterial && (TestMaterial == UpdatedMaterialInterface || TestMaterial->IsDependent(UpdatedMaterialInterface)))
							{
								bPrimitiveIsDependentOnMaterial = true;
								break;
							}
						}
					}
				}

				if (bPrimitiveIsDependentOnMaterial)
				{
					ComponentContexts.Add(new FComponentRecreateRenderStateContext(PrimitiveComponent));
#if WITH_EDITOR
					FStaticLightingSystemInterface::OnPrimitiveComponentUnregistered.Broadcast(PrimitiveComponent);

					if (PrimitiveComponent->HasValidSettingsForStaticLighting(false))
					{
						FStaticLightingSystemInterface::OnPrimitiveComponentRegistered.Broadcast(PrimitiveComponent);
					}
#endif
				}
			}
		}
	}

	ComponentContexts.Empty();
}


/**
 * Shutdown the shader compile manager
 * this function should be used when ending the game to shutdown shader compile threads
 * will not complete current pending shader compilation
 */
void FShaderCompilingManager::Shutdown()
{
	// print the statistics on shutdown
	AllJobs.LogCachingStats(true);

	for (const auto& Thread : Threads)
	{
		Thread->Stop();
		Thread->WaitForCompletion();
	}
}


bool FShaderCompilingManager::HandlePotentialRetryOnError(TMap<int32, FShaderMapFinalizeResults>& CompletedShaderMaps)
{
	bool bRetryCompile = false;

	for (TMap<int32, FShaderMapFinalizeResults>::TIterator It(CompletedShaderMaps); It; ++It)
	{
		FShaderMapFinalizeResults& Results = It.Value();

		if (!Results.bAllJobsSucceeded)
		{
			bool bSpecialEngineMaterial = false;
			const FMaterialShaderMap* ShaderMap = FMaterialShaderMap::FindCompilingShaderMap(It.Key());
			if (ShaderMap)
			{
				for (const FMaterial* Material : ShaderMap->CompilingMaterialDependencies)
				{
					if (Material->IsSpecialEngineMaterial())
					{
						bSpecialEngineMaterial = true;
						break;
					}
				}
			}

#if WITH_EDITORONLY_DATA

			if (UE_LOG_ACTIVE(LogShaders, Log) 
				// Always log detailed errors when a special engine material or global shader fails to compile, as those will be fatal errors
				|| bSpecialEngineMaterial 
				|| It.Key() == GlobalShaderMapId)
			{
				TArray<FShaderCommonCompileJobPtr>& CompleteJobs = Results.FinishedJobs;
				TArray<FShaderCommonCompileJob*> ErrorJobs;
				TArray<FString> UniqueErrors;
				TArray<EShaderPlatform> ErrorPlatforms;

				// Gather unique errors
				for (int32 JobIndex = 0; JobIndex < CompleteJobs.Num(); JobIndex++)
				{
					FShaderCommonCompileJob& CurrentJob = *CompleteJobs[JobIndex];
					if (!CurrentJob.bSucceeded)
					{
						FShaderCompileJob* SingleJob = CurrentJob.GetSingleShaderJob();
						if (SingleJob)
						{
							AddErrorsForFailedJob(*SingleJob, ErrorPlatforms, UniqueErrors, ErrorJobs);
						}
						else
						{
							FShaderPipelineCompileJob* PipelineJob = CurrentJob.GetShaderPipelineJob();
							check(PipelineJob);
							for (auto CommonJob : PipelineJob->StageJobs)
							{
								AddErrorsForFailedJob(*CommonJob, ErrorPlatforms, UniqueErrors, ErrorJobs);
							}
						}
					}
				}

				FString TargetShaderPlatformString;

				for (int32 PlatformIndex = 0; PlatformIndex < ErrorPlatforms.Num(); PlatformIndex++)
				{
					if (TargetShaderPlatformString.IsEmpty())
					{
						TargetShaderPlatformString = LegacyShaderPlatformToShaderFormat(ErrorPlatforms[PlatformIndex]).ToString();
					}
					else
					{
						TargetShaderPlatformString += FString(TEXT(", ")) + LegacyShaderPlatformToShaderFormat(ErrorPlatforms[PlatformIndex]).ToString();
					}
				}

				const TCHAR* MaterialName = ShaderMap ? ShaderMap->GetFriendlyName() : TEXT("global shaders");
				FString ErrorString = FString::Printf(TEXT("%i Shader compiler errors compiling %s for platform %s:"), UniqueErrors.Num(), MaterialName, *TargetShaderPlatformString);
				UE_LOG(LogShaderCompilers, Warning, TEXT("%s"), *ErrorString);
				ErrorString += TEXT("\n");

				for (int32 JobIndex = 0; JobIndex < CompleteJobs.Num(); JobIndex++)
				{
					const FShaderCommonCompileJob& CurrentJob = *CompleteJobs[JobIndex];
					if (!CurrentJob.bSucceeded)
					{
						const auto* SingleJob = CurrentJob.GetSingleShaderJob();
						if (SingleJob)
						{
							ProcessErrors(*SingleJob, UniqueErrors, ErrorString);
						}
						else
						{
							const auto* PipelineJob = CurrentJob.GetShaderPipelineJob();
							check(PipelineJob);
							for (auto CommonJob : PipelineJob->StageJobs)
							{
								ProcessErrors(*CommonJob, UniqueErrors, ErrorString);
							}
						}
					}
				}

				if (UE_LOG_ACTIVE(LogShaders, Log) && bPromptToRetryFailedShaderCompiles)
				{
#if UE_BUILD_DEBUG
					// Use debug break in debug with the debugger attached, otherwise message box
					if (FPlatformMisc::IsDebuggerPresent())
					{
						// A shader compile error has occurred, see the debug output for information.
						// Double click the errors in the VS.NET output window and the IDE will take you directly to the file and line of the error.
						// Check ErrorJobs for more state on the failed shaders, for example in-memory includes like Material.usf
						UE_DEBUG_BREAK();
						// Set GRetryShaderCompilation to true in the debugger to enable retries in debug
						// NOTE: MaterialTemplate.usf will not be reloaded when retrying!
						bRetryCompile = GRetryShaderCompilation;
					}
					else
#endif	//UE_BUILD_DEBUG
					{
						if (FPlatformMisc::MessageBoxExt(EAppMsgType::YesNo, *FText::Format(NSLOCTEXT("UnrealEd", "Error_RetryShaderCompilation", "{0}\r\n\r\nRetry compilation?"),
							FText::FromString(ErrorString)).ToString(), TEXT("Error")) == EAppReturnType::Type::Yes)
						{
							bRetryCompile = true;
						}
					}
				}

				if (bRetryCompile)
				{
					break;
				}
			}
#endif	//WITH_EDITORONLY_DATA
		}
	}

	if (bRetryCompile)
	{
		// Flush the shader file cache so that any changes will be propagated.
		FlushShaderFileCache();

		TArray<int32> MapsToRemove;

		for (TMap<int32, FShaderMapFinalizeResults>::TIterator It(CompletedShaderMaps); It; ++It)
		{
			FShaderMapFinalizeResults& Results = It.Value();

			if (!Results.bAllJobsSucceeded)
			{
				MapsToRemove.Add(It.Key());

				// Reset outputs
				for (int32 JobIndex = 0; JobIndex < Results.FinishedJobs.Num(); JobIndex++)
				{
					FShaderCommonCompileJob& CurrentJob = *Results.FinishedJobs[JobIndex];
					auto* SingleJob = CurrentJob.GetSingleShaderJob();

					// NOTE: Changes to MaterialTemplate.usf before retrying won't work, because the entry for Material.usf in CurrentJob.Environment.IncludeFileNameToContentsMap isn't reset
					if (SingleJob)
					{
						SingleJob->Output = FShaderCompilerOutput();
					}
					else
					{
						auto* PipelineJob = CurrentJob.GetShaderPipelineJob();
						for (auto CommonJob : PipelineJob->StageJobs)
						{
							CommonJob->Output = FShaderCompilerOutput();
							CommonJob->bFinalized = false;
						}
					}
					CurrentJob.bFinalized = false;
				}

				// Send all the shaders from this shader map through the compiler again
				SubmitJobs(Results.FinishedJobs, FString(""), FString(""));
			}
		}

		const int32 OriginalNumShaderMaps = CompletedShaderMaps.Num();

		// Remove the failed shader maps
		for (int32 RemoveIndex = 0; RemoveIndex < MapsToRemove.Num(); RemoveIndex++)
		{
			CompletedShaderMaps.Remove(MapsToRemove[RemoveIndex]);
		}

		check(CompletedShaderMaps.Num() == OriginalNumShaderMaps - MapsToRemove.Num());

		// Block until the failed shader maps have been compiled again
		BlockOnShaderMapCompletion(MapsToRemove, CompletedShaderMaps);

		check(CompletedShaderMaps.Num() == OriginalNumShaderMaps);
	}

	return bRetryCompile;
}

void FShaderMapCompileResults::CheckIfHung()
{
	if (!bIsHung)
	{
		double DurationSoFar = FPlatformTime::Seconds() - TimeStarted;
		if (DurationSoFar >= static_cast<double>(GShaderMapCompilationTimeout))
		{
			bIsHung = true;
			if (GCrashOnHungShaderMaps)
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Crashing on a hung shadermap, time spent compiling: %f seconds, NumPendingJobs: %d, FinishedJobs: %d"),
					DurationSoFar,
					NumPendingJobs.GetValue(),
					FinishedJobs.Num()
				);
			}
			else
			{
				UE_LOG(LogShaderCompilers, Error, TEXT("Hung shadermap detected, time spent compiling: %f seconds, NumPendingJobs: %d, FinishedJobs: %d"),
					DurationSoFar,
					NumPendingJobs.GetValue(),
					FinishedJobs.Num()
				);
			}
		}
	}
}

void FShaderCompilingManager::CancelCompilation(const TCHAR* MaterialName, const TArray<int32>& ShaderMapIdsToCancel)
{
	check(IsInGameThread());
	check(!FPlatformProperties::RequiresCookedData());

	// Lock CompileQueueSection so we can access the input and output queues
	FScopeLock Lock(&CompileQueueSection);

	int32 TotalNumJobsRemoved = 0;
	for (int32 IdIndex = 0; IdIndex < ShaderMapIdsToCancel.Num(); ++IdIndex)
	{
		int32 MapIdx = ShaderMapIdsToCancel[IdIndex];
		if (const FPendingShaderMapCompileResultsPtr* ResultsPtr = ShaderMapJobs.Find(MapIdx))
		{
			const int32 NumJobsRemoved = AllJobs.RemoveAllPendingJobsWithId(MapIdx);
	
			TotalNumJobsRemoved += NumJobsRemoved;

			FShaderMapCompileResults* ShaderMapJob = *ResultsPtr;
			const int32 PrevNumPendingJobs = ShaderMapJob->NumPendingJobs.Subtract(NumJobsRemoved);
			check(PrevNumPendingJobs >= NumJobsRemoved);

			// The shader map job result should be skipped since it is out of date.
			ShaderMapJob->bSkipResultProcessing = true;
		
			if (PrevNumPendingJobs == NumJobsRemoved && ShaderMapJob->FinishedJobs.Num() == 0)
			{
				//We've removed all the jobs for this shader map so remove it.
				ShaderMapJobs.Remove(MapIdx);
			}
		}

		// Don't continue finalizing once compilation has been canceled
		// the CompilingId has been removed from ShaderMapsBeingCompiled, which will cause crash when attempting to do any further processing
		const int32 NumPendingRemoved = PendingFinalizeShaderMaps.Remove(MapIdx);
	}

	if (TotalNumJobsRemoved > 0)
	{
		UE_LOG(LogShaders, Display, TEXT("CancelCompilation %s, Removed %d jobs"), MaterialName ? MaterialName : TEXT(""), TotalNumJobsRemoved);
	}
}

void FShaderCompilingManager::FinishCompilation(const TCHAR* MaterialName, const TArray<int32>& ShaderMapIdsToFinishCompiling)
{
	check(IsInGameThread());
	check(!FPlatformProperties::RequiresCookedData());
	const double StartTime = FPlatformTime::Seconds();

	FText StatusUpdate;
	if ( MaterialName != NULL )
	{
		FFormatNamedArguments Args;
		Args.Add( TEXT("MaterialName"), FText::FromString( MaterialName ) );
		StatusUpdate = FText::Format( NSLOCTEXT("ShaderCompilingManager", "CompilingShadersForMaterialStatus", "Compiling shaders: {MaterialName}..."), Args );
	}
	else
	{
		StatusUpdate = NSLOCTEXT("ShaderCompilingManager", "CompilingShadersStatus", "Compiling shaders...");
	}

	FScopedSlowTask SlowTask(1, StatusUpdate, GIsEditor && !IsRunningCommandlet());
	SlowTask.EnterProgressFrame(1);

	TMap<int32, FShaderMapFinalizeResults> CompiledShaderMaps;
	CompiledShaderMaps.Append( PendingFinalizeShaderMaps );
	PendingFinalizeShaderMaps.Empty();
	BlockOnShaderMapCompletion(ShaderMapIdsToFinishCompiling, CompiledShaderMaps);

	bool bRetry = false;
	do 
	{
		bRetry = HandlePotentialRetryOnError(CompiledShaderMaps);
	} 
	while (bRetry);

	ProcessCompiledShaderMaps(CompiledShaderMaps, FLT_MAX);
	check(CompiledShaderMaps.Num() == 0);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogShaders, Verbose, TEXT("FinishCompilation %s %.3fs"), MaterialName ? MaterialName : TEXT(""), (float)(EndTime - StartTime));
}

void FShaderCompilingManager::FinishAllCompilation()
{
	check(IsInGameThread());
	check(!FPlatformProperties::RequiresCookedData());
	const double StartTime = FPlatformTime::Seconds();

	TMap<int32, FShaderMapFinalizeResults> CompiledShaderMaps;
	CompiledShaderMaps.Append( PendingFinalizeShaderMaps );
	PendingFinalizeShaderMaps.Empty();
	BlockOnAllShaderMapCompletion(CompiledShaderMaps);

	bool bRetry = false;
	do 
	{
		bRetry = HandlePotentialRetryOnError(CompiledShaderMaps);
	} 
	while (bRetry);

	ProcessCompiledShaderMaps(CompiledShaderMaps, FLT_MAX);
	check(CompiledShaderMaps.Num() == 0);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogShaders, Verbose, TEXT("FinishAllCompilation %.3fs"), (float)(EndTime - StartTime));
}

void FShaderCompilingManager::ProcessAsyncResults(bool bLimitExecutionTime, bool bBlockOnGlobalShaderCompletion)
{
	COOK_STAT(FScopedDurationTimer Timer(ShaderCompilerCookStats::ProcessAsyncResultsTimeSec));
	check(IsInGameThread());
	if (bAllowAsynchronousShaderCompiling)
	{
		for (const auto& Thread : Threads)
		{
			Thread->CheckHealth();
		}

		{
			const double StartTime = FPlatformTime::Seconds();

			// Some controllers need to be manually ticked if the engine loop is not initialized or blocked
			// to do things like tick the HTTPModule.
			// Otherwise the results from the controller will never be processed.
			// We check for bBlockOnGlobalShaderCompletion because the BlockOnShaderMapCompletion methods already do this.
			if (!bBlockOnGlobalShaderCompletion && BuildDistributionController)
			{
				BuildDistributionController->Tick(0.0f);
			}
			
			// Block on global shaders before checking for shader maps to finalize
			// So if we block on global shaders for a long time, we will get a chance to finalize all the non-global shader maps completed during that time.
			if (bBlockOnGlobalShaderCompletion)
			{
				TArray<int32> ShaderMapId;
				ShaderMapId.Add(GlobalShaderMapId);

				// Block until the global shader map jobs are complete
				GShaderCompilingManager->BlockOnShaderMapCompletion(ShaderMapId, PendingFinalizeShaderMaps);
			}

			int32 NumCompilingShaderMaps = 0;

			{
				// Lock CompileQueueSection so we can access the input and output queues
				FScopeLock Lock(&CompileQueueSection);

				if (!bBlockOnGlobalShaderCompletion)
				{
					bCompilingDuringGame = true;
				}

				// Get all material shader maps to finalize
				//
				for (TMap<int32, FPendingShaderMapCompileResultsPtr>::TIterator It(ShaderMapJobs); It; ++It)
				{
					FPendingShaderMapCompileResultsPtr& Results = It.Value();
					if (Results->FinishedJobs.Num() > 0)
					{
						FShaderMapFinalizeResults& FinalizeResults = PendingFinalizeShaderMaps.FindOrAdd(It.Key());
						FinalizeResults.FinishedJobs.Append(Results->FinishedJobs);
						Results->FinishedJobs.Reset();
					}

					checkf(Results->FinishedJobs.Num() == 0, TEXT("Failed to remove finished jobs, %d remain"), Results->FinishedJobs.Num());
					if (Results->NumPendingJobs.GetValue() == 0)
					{
						It.RemoveCurrent();
					}
				}

				NumCompilingShaderMaps = ShaderMapJobs.Num();
			}

			int32 NumPendingShaderMaps = PendingFinalizeShaderMaps.Num();

			if (PendingFinalizeShaderMaps.Num() > 0)
			{
				bool bRetry = false;
				do 
				{
					bRetry = HandlePotentialRetryOnError(PendingFinalizeShaderMaps);
				} 
				while (bRetry);

				const float TimeBudget = bLimitExecutionTime ? ProcessGameThreadTargetTime : FLT_MAX;
				ProcessCompiledShaderMaps(PendingFinalizeShaderMaps, TimeBudget);
				check(bLimitExecutionTime || PendingFinalizeShaderMaps.Num() == 0);
			}


			if (bBlockOnGlobalShaderCompletion && !bLimitExecutionTime)
			{
				check(PendingFinalizeShaderMaps.Num() == 0);

				if (NumPendingShaderMaps - PendingFinalizeShaderMaps.Num() > 0)
				{
					UE_LOG(LogShaders, Warning, TEXT("Blocking ProcessAsyncResults for %.1fs, processed %u shader maps, %u being compiled"), 
						(float)(FPlatformTime::Seconds() - StartTime),
						NumPendingShaderMaps - PendingFinalizeShaderMaps.Num(), 
						NumCompilingShaderMaps);
				}
			}
			else if (NumPendingShaderMaps - PendingFinalizeShaderMaps.Num() > 0)
			{
				UE_LOG(LogShaders, Verbose, TEXT("Completed %u async shader maps, %u more pending, %u being compiled"),
					NumPendingShaderMaps - PendingFinalizeShaderMaps.Num(), 
					PendingFinalizeShaderMaps.Num(),
					NumCompilingShaderMaps);
			}
		}
	}
	else
	{
		check(AllJobs.GetNumPendingJobs() == 0);
	}
}

bool FShaderCompilingManager::IsShaderCompilerWorkerRunning(FProcHandle & WorkerHandle)
{
	return FPlatformProcess::IsProcRunning(WorkerHandle);
}

/* Generates a uniform buffer struct member hlsl declaration using the member's metadata. */
static void GenerateUniformBufferStructMember(FString& Result, const FShaderParametersMetadata::FMember& Member, EShaderPlatform ShaderPlatform)
{
	// Generate the base type name.
	FString TypeName;
	Member.GenerateShaderParameterType(TypeName, ShaderPlatform);

	// Generate array dimension post fix
	FString ArrayDim;
	if (Member.GetNumElements() > 0)
	{
		ArrayDim = FString::Printf(TEXT("[%u]"), Member.GetNumElements());
	}

	Result = FString::Printf(TEXT("%s %s%s"), *TypeName, Member.GetName(), *ArrayDim);
}

/* Generates the instanced stereo hlsl code that's dependent on view uniform declarations. */
ENGINE_API void GenerateInstancedStereoCode(FString& Result, EShaderPlatform ShaderPlatform)
{
	// Find the InstancedView uniform buffer struct
	const FShaderParametersMetadata* InstancedView = nullptr;
	for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
	{
		if (StructIt->GetShaderVariableName() == FString(TEXT("InstancedView")))
		{
			InstancedView = *StructIt;
			break;
		}
	}
	checkSlow(InstancedView != nullptr);
	const TArray<FShaderParametersMetadata::FMember>& StructMembers = InstancedView->GetMembers();

	// ViewState definition
	Result =  "struct ViewState\r\n";
	Result += "{\r\n";
	for (int32 MemberIndex = 0; MemberIndex < StructMembers.Num(); ++MemberIndex)
	{
		const FShaderParametersMetadata::FMember& Member = StructMembers[MemberIndex];
		FString MemberDecl;
		GenerateUniformBufferStructMember(MemberDecl, StructMembers[MemberIndex], ShaderPlatform);
		Result += FString::Printf(TEXT("\t%s;\r\n"), *MemberDecl);
	}
	Result += "};\r\n";

	// GetPrimaryView definition
	Result += "ViewState GetPrimaryView()\r\n";
	Result += "{\r\n";
	Result += "\tViewState Result;\r\n";
	for (int32 MemberIndex = 0; MemberIndex < StructMembers.Num(); ++MemberIndex)
	{
		const FShaderParametersMetadata::FMember& Member = StructMembers[MemberIndex];
		Result += FString::Printf(TEXT("\tResult.%s = View.%s;\r\n"), Member.GetName(), Member.GetName());
	}
	Result += "\treturn Result;\r\n";
	Result += "}\r\n";

	// GetInstancedView definition
	Result += "ViewState GetInstancedView()\r\n";
	Result += "{\r\n";
	Result += "\tViewState Result;\r\n";
	for (int32 MemberIndex = 0; MemberIndex < StructMembers.Num(); ++MemberIndex)
	{
		const FShaderParametersMetadata::FMember& Member = StructMembers[MemberIndex];
		Result += FString::Printf(TEXT("\tResult.%s = InstancedView.%s;\r\n"), Member.GetName(), Member.GetName());
	}
	Result += "\treturn Result;\r\n";
	Result += "}\r\n";
	
	// ResolveView definition for metal, this allows us to change the branch to a conditional move in the cross compiler
	Result += "#if COMPILER_METAL && (COMPILER_HLSLCC == 1)\r\n";
	Result += "ViewState ResolveView(uint ViewIndex)\r\n";
	Result += "{\r\n";
	Result += "\tViewState Result;\r\n";
	for (int32 MemberIndex = 0; MemberIndex < StructMembers.Num(); ++MemberIndex)
	{
		const FShaderParametersMetadata::FMember& Member = StructMembers[MemberIndex];
		Result += FString::Printf(TEXT("\tResult.%s = (ViewIndex == 0) ? View.%s : InstancedView.%s;\r\n"), Member.GetName(), Member.GetName(), Member.GetName());
	}
	Result += "\treturn Result;\r\n";
	Result += "}\r\n";
	Result += "#endif\r\n";
}

void ValidateShaderFilePath(const FString& VirtualShaderFilePath, const FString& VirtualSourceFilePath)
{
	check(CheckVirtualShaderFilePath(VirtualShaderFilePath));

	checkf(VirtualShaderFilePath.Contains(TEXT("/Generated/")),
		TEXT("Incorrect virtual shader path for generated file '%s': Generated files must be located under an "
				"non existing 'Generated' directory, for instance: /Engine/Generated/ or /Plugin/FooBar/Generated/."),
		*VirtualShaderFilePath);

	checkf(VirtualShaderFilePath == VirtualSourceFilePath || FPaths::GetExtension(VirtualShaderFilePath) == TEXT("ush"),
		TEXT("Incorrect virtual shader path extension for generated file '%s': Generated file must either be the "
				"USF to compile, or a USH file to be included."),
		*VirtualShaderFilePath);
}


static void PullRootShaderParametersLayout(FShaderCompilerInput& CompileInput, EShaderPlatform ShaderPlatform, const FShaderParametersMetadata& ParametersMetadata, uint16 ByteOffset, const FString& Prefix)
{
	for (const FShaderParametersMetadata::FMember& Member : ParametersMetadata.GetMembers())
	{
		EUniformBufferBaseType BaseType = Member.GetBaseType();
		uint16 MemberOffset = ByteOffset + uint16(Member.GetOffset());
		uint32 NumElements = Member.GetNumElements();

		if (BaseType == UBMT_INCLUDED_STRUCT)
		{
			check(NumElements == 0);
			PullRootShaderParametersLayout(CompileInput, ShaderPlatform, *Member.GetStructMetadata(), MemberOffset, Prefix);
		}
		else if (BaseType == UBMT_NESTED_STRUCT && NumElements == 0)
		{
			FString NewPrefix = FString::Printf(TEXT("%s%s_"), *Prefix, Member.GetName());
			PullRootShaderParametersLayout(CompileInput, ShaderPlatform, *Member.GetStructMetadata(), MemberOffset, NewPrefix);
		}
		else if (BaseType == UBMT_NESTED_STRUCT && NumElements > 0)
		{
			for (uint32 ArrayElementId = 0; ArrayElementId < NumElements; ArrayElementId++)
			{
				FString NewPrefix = FString::Printf(TEXT("%s%s_%u_"), *Prefix, Member.GetName(), ArrayElementId);
				PullRootShaderParametersLayout(CompileInput, ShaderPlatform, *Member.GetStructMetadata(), MemberOffset, NewPrefix);
			}
		}
		else if (
			BaseType == UBMT_INT32 ||
			BaseType == UBMT_UINT32 ||
			BaseType == UBMT_FLOAT32)
		{
			FShaderCompilerInput::FRootParameterBinding RootParameterBinding;
			RootParameterBinding.Name = FString::Printf(TEXT("%s%s"), *Prefix, Member.GetName());
			Member.GenerateShaderParameterType(RootParameterBinding.ExpectedShaderType, ShaderPlatform);
			RootParameterBinding.ByteOffset = MemberOffset;
			CompileInput.RootParameterBindings.Add(RootParameterBinding);
		}
		continue;

		if (BaseType == UBMT_REFERENCED_STRUCT)
		{
			// Referenced structured are manually passed to the RHI.
		}
		else if (BaseType == UBMT_RENDER_TARGET_BINDING_SLOTS)
		{
			// RHI don't need to care about render target bindings slot anyway.
		}
		else if (
			BaseType == UBMT_RDG_BUFFER_ACCESS ||
			BaseType == UBMT_RDG_TEXTURE_ACCESS)
		{
			// Shaders don't care about RDG access parameters.
		}
		else if (
			BaseType == UBMT_RDG_BUFFER_UAV ||
			BaseType == UBMT_RDG_TEXTURE_UAV)
		{
			// UAV are ignored on purpose because not supported in uniform buffers.
		}
		else
		{
			check(0);
		}
	}
}

FThreadSafeSharedStringPtr GCachedGeneratedInstancedStereoCode = MakeShareable(new FString());

/** Enqueues a shader compile job with GShaderCompilingManager. */
void GlobalBeginCompileShader(
	const FString& DebugGroupName,
	const FVertexFactoryType* VFType,
	const FShaderType* ShaderType,
	const FShaderPipelineType* ShaderPipelineType,
	int32 PermutationId,
	const TCHAR* SourceFilename,
	const TCHAR* FunctionName,
	FShaderTarget Target,
	FShaderCompilerInput& Input,
	bool bAllowDevelopmentShaderCompile,
	const FString& DebugDescription,
	const FString& DebugExtension

	)
{
	COOK_STAT(ShaderCompilerCookStats::GlobalBeginCompileShaderCalls++);
	COOK_STAT(FScopedDurationTimer DurationTimer(ShaderCompilerCookStats::GlobalBeginCompileShaderTimeSec));

	EShaderPlatform ShaderPlatform = EShaderPlatform(Target.Platform);

	Input.Target = Target;
	Input.ShaderFormat = LegacyShaderPlatformToShaderFormat(ShaderPlatform);
	Input.VirtualSourceFilePath = SourceFilename;
	Input.EntryPointName = FunctionName;
	Input.bCompilingForShaderPipeline = false;
	Input.bIncludeUsedOutputs = false;
	Input.bGenerateDirectCompileFile = (GDumpShaderDebugInfoSCWCommandLine != 0);
	Input.DumpDebugInfoRootPath = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / Input.ShaderFormat.ToString();
	// asset material name or "Global"
	Input.DebugGroupName = DebugGroupName;
	Input.DebugDescription = DebugDescription;
	Input.DebugExtension = DebugExtension;

	if (ShaderType->GetRootParametersMetadata())
	{
		PullRootShaderParametersLayout(Input, ShaderPlatform, *ShaderType->GetRootParametersMetadata(), /* ByteOffset = */ 0, FString());
	}

	// Verify FShaderCompilerInput's file paths are consistent. 
	#if DO_CHECK
		check(CheckVirtualShaderFilePath(Input.VirtualSourceFilePath));

		checkf(FPaths::GetExtension(Input.VirtualSourceFilePath) == TEXT("usf"),
			TEXT("Incorrect virtual shader path extension for shader file to compile '%s': Only .usf files should be "
				 "compiled. .ush file are meant to be included only."),
			*Input.VirtualSourceFilePath);

		for (const auto& Entry : Input.Environment.IncludeVirtualPathToContentsMap)
		{
			ValidateShaderFilePath(Entry.Key, Input.VirtualSourceFilePath);
		}

		for (const auto& Entry : Input.Environment.IncludeVirtualPathToExternalContentsMap)
		{
			ValidateShaderFilePath(Entry.Key, Input.VirtualSourceFilePath);
		}
	#endif

	if (ShaderPipelineType)
	{
		Input.DebugGroupName = Input.DebugGroupName / ShaderPipelineType->GetName();
	}
	
	if (VFType)
	{
		FString VFName = VFType->GetName();
		if (GDumpShaderDebugInfoShort)
		{
			// Shorten vertex factory name
			if (VFName[0] == TCHAR('F') || VFName[0] == TCHAR('T'))
			{
				VFName.RemoveAt(0);
			}
			VFName.ReplaceInline(TEXT("VertexFactory"), TEXT("VF"));
			VFName.ReplaceInline(TEXT("GPUSkinAPEXCloth"), TEXT("APEX"));
			VFName.ReplaceInline(TEXT("true"), TEXT("_1"));
			VFName.ReplaceInline(TEXT("false"), TEXT("_0"));
		}
		Input.DebugGroupName = Input.DebugGroupName / VFName;
	}
	
	{
		FString ShaderTypeName = ShaderType->GetName();
		if (GDumpShaderDebugInfoShort)
		{
			// Shorten known types
			if (ShaderTypeName[0] == TCHAR('F') || ShaderTypeName[0] == TCHAR('T'))
			{
				ShaderTypeName.RemoveAt(0);
			}
		}
		Input.DebugGroupName = Input.DebugGroupName / ShaderTypeName / FString::Printf(TEXT("%i"), PermutationId);
		
		if (GDumpShaderDebugInfoShort)
		{
			Input.DebugGroupName.ReplaceInline(TEXT("BasePass"), TEXT("BP"));
			Input.DebugGroupName.ReplaceInline(TEXT("ForForward"), TEXT("Fwd"));
			Input.DebugGroupName.ReplaceInline(TEXT("Shadow"), TEXT("Shdw"));
			Input.DebugGroupName.ReplaceInline(TEXT("LightMap"), TEXT("LM"));
			Input.DebugGroupName.ReplaceInline(TEXT("EHeightFogFeature==E_"), TEXT(""));
			Input.DebugGroupName.ReplaceInline(TEXT("Capsule"), TEXT("Caps"));
			Input.DebugGroupName.ReplaceInline(TEXT("Movable"), TEXT("Mov"));
			Input.DebugGroupName.ReplaceInline(TEXT("Culling"), TEXT("Cull"));
			Input.DebugGroupName.ReplaceInline(TEXT("Atmospheric"), TEXT("Atm"));
			Input.DebugGroupName.ReplaceInline(TEXT("Atmosphere"), TEXT("Atm"));
			Input.DebugGroupName.ReplaceInline(TEXT("Exponential"), TEXT("Exp"));
			Input.DebugGroupName.ReplaceInline(TEXT("Ambient"), TEXT("Amb"));
			Input.DebugGroupName.ReplaceInline(TEXT("Perspective"), TEXT("Persp"));
			Input.DebugGroupName.ReplaceInline(TEXT("Occlusion"), TEXT("Occ"));
			Input.DebugGroupName.ReplaceInline(TEXT("Position"), TEXT("Pos"));
			Input.DebugGroupName.ReplaceInline(TEXT("Skylight"), TEXT("Sky"));
			Input.DebugGroupName.ReplaceInline(TEXT("LightingPolicy"), TEXT("LP"));
			Input.DebugGroupName.ReplaceInline(TEXT("TranslucentLighting"), TEXT("TranslLight"));
			Input.DebugGroupName.ReplaceInline(TEXT("Translucency"), TEXT("Transl"));
			Input.DebugGroupName.ReplaceInline(TEXT("DistanceField"), TEXT("DistFiel"));
			Input.DebugGroupName.ReplaceInline(TEXT("Indirect"), TEXT("Ind"));
			Input.DebugGroupName.ReplaceInline(TEXT("Cached"), TEXT("Cach"));
			Input.DebugGroupName.ReplaceInline(TEXT("Inject"), TEXT("Inj"));
			Input.DebugGroupName.ReplaceInline(TEXT("Visualization"), TEXT("Viz"));
			Input.DebugGroupName.ReplaceInline(TEXT("Instanced"), TEXT("Inst"));
			Input.DebugGroupName.ReplaceInline(TEXT("Evaluate"), TEXT("Eval"));
			Input.DebugGroupName.ReplaceInline(TEXT("Landscape"), TEXT("Land"));
			Input.DebugGroupName.ReplaceInline(TEXT("Dynamic"), TEXT("Dyn"));
			Input.DebugGroupName.ReplaceInline(TEXT("Vertex"), TEXT("Vtx"));
			Input.DebugGroupName.ReplaceInline(TEXT("Output"), TEXT("Out"));
			Input.DebugGroupName.ReplaceInline(TEXT("Directional"), TEXT("Dir"));
			Input.DebugGroupName.ReplaceInline(TEXT("Irradiance"), TEXT("Irr"));
			Input.DebugGroupName.ReplaceInline(TEXT("Deferred"), TEXT("Def"));
			Input.DebugGroupName.ReplaceInline(TEXT("true"), TEXT("_1"));
			Input.DebugGroupName.ReplaceInline(TEXT("false"), TEXT("_0"));
			Input.DebugGroupName.ReplaceInline(TEXT("PROPAGATE_AO"), TEXT("AO"));
			Input.DebugGroupName.ReplaceInline(TEXT("PROPAGATE_SECONDARY_OCCLUSION"), TEXT("SEC_OCC"));
			Input.DebugGroupName.ReplaceInline(TEXT("PROPAGATE_MULTIPLE_BOUNCES"), TEXT("MULT_BOUNC"));
			Input.DebugGroupName.ReplaceInline(TEXT("PostProcess"), TEXT("Post"));
			Input.DebugGroupName.ReplaceInline(TEXT("AntiAliasing"), TEXT("AA"));
			Input.DebugGroupName.ReplaceInline(TEXT("Mobile"), TEXT("Mob"));
			Input.DebugGroupName.ReplaceInline(TEXT("Linear"), TEXT("Lin"));
			Input.DebugGroupName.ReplaceInline(TEXT("INT32_MAX"), TEXT("IMAX"));
			Input.DebugGroupName.ReplaceInline(TEXT("Policy"), TEXT("Pol"));
			Input.DebugGroupName.ReplaceInline(TEXT("EAtmRenderFlag==E_"), TEXT(""));
		}
	}

	// Setup the debug info path if requested, or if this is a global shader and shader development mode is enabled
	Input.DumpDebugInfoPath.Empty();
	if (GShaderCompilingManager->GetDumpShaderDebugInfo() == FShaderCompilingManager::EDumpShaderDebugInfo::Always)
	{
		Input.DumpDebugInfoPath = GShaderCompilingManager->CreateShaderDebugInfoPath(Input);
	}

	// Add the appropriate definitions for the shader frequency.
	{
		Input.Environment.SetDefine(TEXT("PIXELSHADER"), Target.Frequency == SF_Pixel);
		Input.Environment.SetDefine(TEXT("DOMAINSHADER"), Target.Frequency == SF_Domain);
		Input.Environment.SetDefine(TEXT("HULLSHADER"), Target.Frequency == SF_Hull);
		Input.Environment.SetDefine(TEXT("VERTEXSHADER"), Target.Frequency == SF_Vertex);
		Input.Environment.SetDefine(TEXT("GEOMETRYSHADER"), Target.Frequency == SF_Geometry);
		Input.Environment.SetDefine(TEXT("COMPUTESHADER"), Target.Frequency == SF_Compute);
		Input.Environment.SetDefine(TEXT("RAYCALLABLESHADER"), Target.Frequency == SF_RayCallable);
		Input.Environment.SetDefine(TEXT("RAYHITGROUPSHADER"), Target.Frequency == SF_RayHitGroup);
		Input.Environment.SetDefine(TEXT("RAYGENSHADER"), Target.Frequency == SF_RayGen);
		Input.Environment.SetDefine(TEXT("RAYMISSSHADER"), Target.Frequency == SF_RayMiss);
	}

	// #defines get stripped out by the preprocessor without this. We can override with this
	Input.Environment.SetDefine(TEXT("COMPILER_DEFINE"), TEXT("#define"));

	if (FSceneInterface::GetShadingPath(GetMaxSupportedFeatureLevel(ShaderPlatform)) == EShadingPath::Deferred)
	{
		Input.Environment.SetDefine(TEXT("SHADING_PATH_DEFERRED"), 1);
	}

	const bool bUsingMobileRenderer = FSceneInterface::GetShadingPath(GetMaxSupportedFeatureLevel(ShaderPlatform)) == EShadingPath::Mobile;
	if (bUsingMobileRenderer)
	{
		Input.Environment.SetDefine(TEXT("SHADING_PATH_MOBILE"), 1);
		if (IsMobileDeferredShadingEnabled((EShaderPlatform)Target.Platform))
		{
			Input.Environment.SetDefine(TEXT("MOBILE_DEFERRED_SHADING"), 1);
		}
	}

	// Set VR definitions
	{
		static const auto CVarInstancedStereo = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.InstancedStereo"));
		static const auto CVarMobileMultiView = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));
		static const auto CVarODSCapture = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.ODSCapture"));
		static const auto CVarMobileHDR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));

		const bool bIsInstancedStereoCVar = CVarInstancedStereo ? (CVarInstancedStereo->GetValueOnAnyThread() != 0) : false;
		const bool bIsMobileMultiViewCVar = CVarMobileMultiView && CVarMobileHDR ?
			(CVarMobileMultiView->GetValueOnAnyThread() != 0 && CVarMobileHDR->GetValueOnAnyThread() == 0) : false;
		const bool bIsODSCapture = CVarODSCapture && (CVarODSCapture->GetValueOnAnyThread() != 0);

		bool bIsInstancedStereo = !bUsingMobileRenderer && bIsInstancedStereoCVar && RHISupportsInstancedStereo(ShaderPlatform);
		bool bIsMobileMultiview = bUsingMobileRenderer && bIsMobileMultiViewCVar;
		if (bIsMobileMultiview && !RHISupportsMobileMultiView(ShaderPlatform))
		{
			// Native mobile multi-view is not supported, fall back to instancing if available
			bIsMobileMultiview = bIsInstancedStereo = RHISupportsInstancedStereo(ShaderPlatform);
		}

		Input.Environment.SetDefine(TEXT("INSTANCED_STEREO"), bIsInstancedStereo);
		Input.Environment.SetDefine(TEXT("MULTI_VIEW"), bIsInstancedStereo && RHISupportsMultiView(ShaderPlatform));
		Input.Environment.SetDefine(TEXT("MOBILE_MULTI_VIEW"), bIsMobileMultiview);

		// Throw a warning if we are silently disabling ISR due to missing platform support.
		if (bIsInstancedStereoCVar && !bIsInstancedStereo && !GShaderCompilingManager->AreWarningsSuppressed(ShaderPlatform))
		{
			UE_LOG(LogShaderCompilers, Log, TEXT("Instanced stereo rendering is not supported for the %s shader platform."), *LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString());
			GShaderCompilingManager->SuppressWarnings(ShaderPlatform);
		}

		Input.Environment.SetDefine(TEXT("ODS_CAPTURE"), bIsODSCapture);
	}

	ShaderType->AddReferencedUniformBufferIncludes(Input.Environment, Input.SourceFilePrefix, ShaderPlatform);

	if (VFType)
	{
		VFType->AddReferencedUniformBufferIncludes(Input.Environment, Input.SourceFilePrefix, ShaderPlatform);
	}

	// Add generated instanced stereo code
	if (GCachedGeneratedInstancedStereoCode.Get()->Len() == 0)
	{
		GCachedGeneratedInstancedStereoCode = MakeShareable(new FString());
		GenerateInstancedStereoCode(*GCachedGeneratedInstancedStereoCode.Get(), ShaderPlatform);
	}
	
	Input.Environment.IncludeVirtualPathToExternalContentsMap.Add(TEXT("/Engine/Generated/GeneratedInstancedStereo.ush"), GCachedGeneratedInstancedStereoCode);

	{
		// Check if the compile environment explicitly wants to force optimization
		const bool bForceOptimization = Input.Environment.CompilerFlags.Contains(CFLAG_ForceOptimization);

		static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.Optimize"));
		if (!bForceOptimization && CVar && CVar->GetInt() == 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_Debug);
		}
	}

	{
		if (ShouldKeepShaderDebugInfo((EShaderPlatform)Target.Platform))
		{
			Input.Environment.CompilerFlags.Add(CFLAG_KeepDebugInfo);
		}
	}

	if (CVarShaderFastMath.GetValueOnAnyThread() == 0)
	{
		Input.Environment.CompilerFlags.Add(CFLAG_NoFastMath);
	}
	
	{
		int32 FlowControl = CVarShaderFlowControl.GetValueOnAnyThread();
		switch (FlowControl)
		{
			case 2:
				Input.Environment.CompilerFlags.Add(CFLAG_AvoidFlowControl);
				break;
			case 1:
				Input.Environment.CompilerFlags.Add(CFLAG_PreferFlowControl);
				break;
			case 0:
				// Fallback to nothing...
			default:
				break;
		}
	}

	{
		static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.Validation"));
		if (CVar && CVar->GetInt() == 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_SkipValidation);
		}
	}

	if (IsD3DPlatform((EShaderPlatform)Target.Platform) && IsPCPlatform((EShaderPlatform)Target.Platform))
	{
		if (CVarD3DRemoveUnusedInterpolators.GetValueOnAnyThread() != 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_ForceRemoveUnusedInterpolators);
		}

		if (CVarD3DCheckedForTypedUAVs.GetValueOnAnyThread() == 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		}

		if (CVarD3DForceShaderConductorDXCRewrite.GetValueOnAnyThread() != 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_D3D12ForceShaderConductorRewrite);
		}
	}

	if (IsMetalPlatform((EShaderPlatform)Target.Platform))
	{
		if (CVarShaderZeroInitialise.GetValueOnAnyThread() != 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_ZeroInitialise);
		}

		if (CVarShaderBoundsChecking.GetValueOnAnyThread() != 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_BoundsChecking);
		}
		
		// Check whether we can compile metal shaders to bytecode - avoids poisoning the DDC
		static ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
		const FName Format = LegacyShaderPlatformToShaderFormat(EShaderPlatform(Target.Platform));
		const IShaderFormat* Compiler = TPM.FindShaderFormat(Format);
		static const bool bCanCompileOfflineMetalShaders = Compiler && Compiler->CanCompileBinaryShaders();
		if (!bCanCompileOfflineMetalShaders)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_Debug);
		}
		else
		{
			// populate the data in the shader input environment
			FString RemoteServer;
			FString UserName;
			FString SSHKey;
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("RemoteServerName"), RemoteServer, GEngineIni);
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("RSyncUsername"), UserName, GEngineIni);
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("SSHPrivateKeyOverridePath"), SSHKey, GEngineIni);
			Input.Environment.RemoteServerData.Add(TEXT("RemoteServerName"), RemoteServer);
			Input.Environment.RemoteServerData.Add(TEXT("RSyncUsername"), UserName);
			if (SSHKey.Len() > 0)
			{
				Input.Environment.RemoteServerData.Add(TEXT("SSHPrivateKeyOverridePath"), SSHKey);
			}
		}
		
		// Shaders built for archiving - for Metal that requires compiling the code in a different way so that we can strip it later
		bool bArchive = false;
		GConfig->GetBool(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("bSharedMaterialNativeLibraries"), bArchive, GGameIni);
		if (bCanCompileOfflineMetalShaders && bArchive)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_Archive);
		}
		
		{
			uint32 ShaderVersion = RHIGetShaderLanguageVersion(EShaderPlatform(Target.Platform));
			Input.Environment.SetDefine(TEXT("MAX_SHADER_LANGUAGE_VERSION"), ShaderVersion);
			
			bool bAllowFastIntrinsics = false;
			bool bForceFloats = false;
			FString IndirectArgumentTier;
			bool bEnableMathOptimisations = true;
			if (IsPCPlatform(EShaderPlatform(Target.Platform)))
			{
				GConfig->GetBool(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("UseFastIntrinsics"), bAllowFastIntrinsics, GEngineIni);
				GConfig->GetBool(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("EnableMathOptimisations"), bEnableMathOptimisations, GEngineIni);
				GConfig->GetBool(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("ForceFloats"), bForceFloats, GEngineIni);
				GConfig->GetString(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("IndirectArgumentTier"), IndirectArgumentTier, GEngineIni);
			}
			else
			{
				GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("UseFastIntrinsics"), bAllowFastIntrinsics, GEngineIni);
				GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("EnableMathOptimisations"), bEnableMathOptimisations, GEngineIni);
				GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("ForceFloats"), bForceFloats, GEngineIni);
				GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("IndirectArgumentTier"), IndirectArgumentTier, GEngineIni);
				// Force no development shaders on iOS
				bAllowDevelopmentShaderCompile = false;
			}
			Input.Environment.SetDefine(TEXT("METAL_USE_FAST_INTRINSICS"), bAllowFastIntrinsics);
			Input.Environment.SetDefine(TEXT("FORCE_FLOATS"), bForceFloats);
			Input.Environment.SetDefine(TEXT("METAL_INDIRECT_ARGUMENT_BUFFERS"), IndirectArgumentTier);
			
			// Same as console-variable above, but that's global and this is per-platform, per-project
			if (!bEnableMathOptimisations)
			{
				Input.Environment.CompilerFlags.Add(CFLAG_NoFastMath);
			}
		}
	}

	// Add compiler flag CFLAG_ForceDXC if DXC is enabled
	const bool bIsDxcEnabled = IsDxcEnabledForPlatform((EShaderPlatform)Target.Platform);
	Input.Environment.SetDefine(TEXT("COMPILER_DXC"), bIsDxcEnabled);
	if (bIsDxcEnabled)
	{
		Input.Environment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	if (IsMobilePlatform((EShaderPlatform)Target.Platform))
	{
		if (IsOpenGLPlatform((EShaderPlatform)Target.Platform))
		{
			static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("OpenGL.UseEmulatedUBs"));
			if (CVar && CVar->GetInt() != 0)
			{
				Input.Environment.CompilerFlags.Add(CFLAG_UseEmulatedUB);
			}
		}
		else if(IsVulkanPlatform((EShaderPlatform)Target.Platform))
		{
			static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Vulkan.UseRealUBs"));
			if ((CVar && CVar->GetInt() == 0) || 
				Target.Platform == SP_VULKAN_ES3_1_ANDROID) // we force eUB on mobile Android
			{
				Input.Environment.CompilerFlags.Add(CFLAG_UseEmulatedUB);
			}
		}
	}
	else
	{
		static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Vulkan.UseRealUBs"));
		if (CVar && CVar->GetInt() == 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_UseEmulatedUB);
		}
	}

	Input.Environment.SetDefine(TEXT("HAS_INVERTED_Z_BUFFER"), (bool)ERHIZBuffer::IsInverted);

	{
		FString ShaderPDBRoot;
		GConfig->GetString(TEXT("DevOptions.Shaders"), TEXT("ShaderPDBRoot"), ShaderPDBRoot, GEngineIni);
		if (!ShaderPDBRoot.IsEmpty())
		{
			Input.Environment.SetDefine(TEXT("SHADER_PDB_ROOT"), ShaderPDBRoot);
		}
	}

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ClearCoatNormal"));
		Input.Environment.SetDefine(TEXT("CLEAR_COAT_BOTTOM_NORMAL"), CVar ? (CVar->GetValueOnAnyThread() != 0) : 0);
	}

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.IrisNormal"));
		Input.Environment.SetDefine(TEXT("IRIS_NORMAL"), CVar ? (CVar->GetValueOnAnyThread() != 0) : 0);
	}

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("Compat.UseDXT5NormalMaps"));
		Input.Environment.SetDefine(TEXT("DXT5_NORMALMAPS"), CVar ? (CVar->GetValueOnAnyThread() != 0) : 0);
	}

	if (bAllowDevelopmentShaderCompile)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.CompileShadersForDevelopment"));
		Input.Environment.SetDefine(TEXT("COMPILE_SHADERS_FOR_DEVELOPMENT"), CVar ? (CVar->GetValueOnAnyThread() != 0) : 0);
	}

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		Input.Environment.SetDefine(TEXT("ALLOW_STATIC_LIGHTING"), CVar ? (CVar->GetValueOnAnyThread() != 0) : 1);
	}

	{
		Input.Environment.SetDefine(TEXT("GBUFFER_HAS_VELOCITY"), IsUsingBasePassVelocity((EShaderPlatform)Target.Platform) ? 1 : 0);
	}

	{
		Input.Environment.SetDefine(TEXT("SELECTIVE_BASEPASS_OUTPUTS"), IsUsingSelectiveBasePassOutputs((EShaderPlatform)Target.Platform) ? 1 : 0);
	}

	{
		// PreExposure is now always enabled in the shaders.
		Input.Environment.SetDefine(TEXT("USE_PREEXPOSURE"), 1);
	}

	{
		Input.Environment.SetDefine(TEXT("USE_DBUFFER"), IsUsingDBuffers((EShaderPlatform)Target.Platform) ? 1 : 0);
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowGlobalClipPlane"));
		Input.Environment.SetDefine(TEXT("PROJECT_ALLOW_GLOBAL_CLIP_PLANE"), CVar ? (CVar->GetInt() != 0) : 0);
	}

	ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->FindTargetPlatformWithSupport(TEXT("ShaderFormat"), LegacyShaderPlatformToShaderFormat((EShaderPlatform)Target.Platform));
	bool bForwardShading = false;
	{
		if (TargetPlatform)
		{
			bForwardShading = TargetPlatform->UsesForwardShading();
		}
		else
		{
			static IConsoleVariable* CVarForwardShading = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ForwardShading"));
			bForwardShading = CVarForwardShading ? (CVarForwardShading->GetInt() != 0) : false;
		}
		Input.Environment.SetDefine(TEXT("FORWARD_SHADING"), bForwardShading);
	}

	{
		if (VelocityEncodeDepth((EShaderPlatform)Target.Platform))
		{
			Input.Environment.SetDefine(TEXT("VELOCITY_ENCODE_DEPTH"), 1);
		}
		else
		{
			Input.Environment.SetDefine(TEXT("VELOCITY_ENCODE_DEPTH"), 0);
		}
	}

	{
		if (MaskedInEarlyPass((EShaderPlatform)Target.Platform))
		{
			Input.Environment.SetDefine(TEXT("EARLY_Z_PASS_ONLY_MATERIAL_MASKING"), 1);
		}
		else
		{
			Input.Environment.SetDefine(TEXT("EARLY_Z_PASS_ONLY_MATERIAL_MASKING"), 0);
		}
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VertexFoggingForOpaque"));
		bool bVertexFoggingForOpaque = false;
		if (bForwardShading)
		{
			bVertexFoggingForOpaque = CVar ? (CVar->GetInt() != 0) : 0;
			if (TargetPlatform)
			{
				const int32 PlatformHeightFogMode = TargetPlatform->GetHeightFogModeForOpaque();
				if (PlatformHeightFogMode == 1)
				{
					bVertexFoggingForOpaque = false;
				}
				else if (PlatformHeightFogMode == 2)
				{
					bVertexFoggingForOpaque = true;
				}
			}
		}
		Input.Environment.SetDefine(TEXT("PROJECT_VERTEX_FOGGING_FOR_OPAQUE"), bVertexFoggingForOpaque);
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.DisableVertexFog"));
		Input.Environment.SetDefine(TEXT("PROJECT_MOBILE_DISABLE_VERTEX_FOG"), CVar ? (CVar->GetInt() != 0) : 0);
	}

	bool bSupportSkyAtmosphere = false;
	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SupportSkyAtmosphere"));
		bSupportSkyAtmosphere = CVar && CVar->GetInt() != 0;
		Input.Environment.SetDefine(TEXT("PROJECT_SUPPORT_SKY_ATMOSPHERE"), bSupportSkyAtmosphere ? 1 : 0);
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SupportSkyAtmosphereAffectsHeightFog"));
		Input.Environment.SetDefine(TEXT("PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG"), (CVar && bSupportSkyAtmosphere) ? (CVar->GetInt() != 0) : 0);
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.ForceFullPrecisionInPS"));
		if (CVar && CVar->GetInt() != 0)
		{
			Input.Environment.CompilerFlags.Add(CFLAG_UseFullPrecisionInPS);
		}
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessing.PropagateAlpha"));
		int32 PropagateAlpha = CVar->GetInt();
		if (PropagateAlpha < 0 || PropagateAlpha > 2)
		{
			PropagateAlpha = 0;
		}
		Input.Environment.SetDefine(TEXT("POST_PROCESS_ALPHA"), PropagateAlpha);
	}

	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DistanceFieldBuild.EightBit"));
		Input.Environment.SetDefine(TEXT("EIGHT_BIT_MESH_DISTANCE_FIELDS"), CVar ? (CVar->GetInt() != 0) : 0);
	}

	Input.Environment.SetDefine(TEXT("PLATFORM_SUPPORTS_RENDERTARGET_WRITE_MASK"), RHISupportsRenderTargetWriteMask(EShaderPlatform(Target.Platform)) ? 1 : 0);
	Input.Environment.SetDefine(TEXT("PLATFORM_SUPPORTS_PER_PIXEL_DBUFFER_MASK"), FDataDrivenShaderPlatformInfo::GetSupportsPerPixelDBufferMask(EShaderPlatform(Target.Platform)) ? 1 : 0);
	Input.Environment.SetDefine(TEXT("PLATFORM_SUPPORTS_DISTANCE_FIELDS"), DoesPlatformSupportDistanceFields(EShaderPlatform(Target.Platform)) ? 1 : 0);
	
	{
		static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.vt.FeedbackFactor"));
		Input.Environment.SetDefine(TEXT("VIRTUAL_TEXTURE_FEEDBACK_FACTOR"), CVar ? FMath::RoundUpToPowerOfTwo(FMath::Max(CVar->GetInt(), 1)) : 1);
	}


	if (IsMobilePlatform((EShaderPlatform)Target.Platform))
	{
		static FShaderPlatformCachedIniValue<bool> MobileEnableMovableSpotlightsIniValue(TEXT("/Script/Engine.RendererSettings"), TEXT("r.Mobile.EnableMovableSpotlights"));
		static FShaderPlatformCachedIniValue<bool> MobileEnableMovableSpotlightsShadowIniValue(TEXT("/Script/Engine.RendererSettings"), TEXT("r.Mobile.EnableMovableSpotlightsShadow"));

		bool bMobileEnableMovableSpotlights = (MobileEnableMovableSpotlightsIniValue.Get((EShaderPlatform)Target.Platform) != 0);
		Input.Environment.SetDefine(TEXT("PROJECT_MOBILE_ENABLE_MOVABLE_SPOTLIGHTS"), bMobileEnableMovableSpotlights ? 1 : 0);

		bool bMobileEnableMovableSpotlightsShadow = (MobileEnableMovableSpotlightsShadowIniValue.Get((EShaderPlatform)Target.Platform) != 0);
		Input.Environment.SetDefine(TEXT("PROJECT_MOBILE_ENABLE_MOVABLE_SPOTLIGHTS_SHADOW"), bMobileEnableMovableSpotlights && bMobileEnableMovableSpotlightsShadow ? 1 : 0);
	}

	// Allow the target shader format to modify the shader input before we add it as a job
	const IShaderFormat* Format = GetTargetPlatformManagerRef().FindShaderFormat(Input.ShaderFormat);
	Format->ModifyShaderCompilerInput(Input);
}


/** Timer class used to report information on the 'recompileshaders' console command. */
class FRecompileShadersTimer
{
public:
	FRecompileShadersTimer(const TCHAR* InInfoStr=TEXT("Test")) :
		InfoStr(InInfoStr),
		bAlreadyStopped(false)
	{
		StartTime = FPlatformTime::Seconds();
	}

	FRecompileShadersTimer(const FString& InInfoStr) :
		InfoStr(InInfoStr),
		bAlreadyStopped(false)
	{
		StartTime = FPlatformTime::Seconds();
	}

	void Stop(bool DisplayLog = true)
	{
		if (!bAlreadyStopped)
		{
			bAlreadyStopped = true;
			EndTime = FPlatformTime::Seconds();
			TimeElapsed = EndTime-StartTime;
			if (DisplayLog)
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("		[%s] took [%.4f] s"),*InfoStr,TimeElapsed);
			}
		}
	}

	~FRecompileShadersTimer()
	{
		Stop(true);
	}

protected:
	double StartTime,EndTime;
	double TimeElapsed;
	FString InfoStr;
	bool bAlreadyStopped;
};

namespace
{
	bool ParseRecompileCommandString(const TCHAR* CmdString, TArray<FString>& OutMaterialsToLoad)
	{
		FString CmdName = FParse::Token(CmdString, 0);

		bool bCompileChangedShaders = true;
		OutMaterialsToLoad.Empty();

		if( !CmdName.IsEmpty() && FCString::Stricmp(*CmdName,TEXT("Material"))==0 )
		{
			bCompileChangedShaders = false;

			// tell other side the material to load, by pathname
			FString RequestedMaterialName( FParse::Token( CmdString, 0 ) );

			for( TObjectIterator<UMaterialInterface> It; It; ++It )
			{
				UMaterial* Material = It->GetMaterial();

				if( Material && Material->GetName() == RequestedMaterialName)
				{
					OutMaterialsToLoad.Add(It->GetPathName());
					break;
				}
			}
		}
		else
		{
			// tell other side all the materials to load, by pathname
			for( TObjectIterator<UMaterialInterface> It; It; ++It )
			{
				OutMaterialsToLoad.Add(It->GetPathName());
			}
		}

		return bCompileChangedShaders;
	}
}

void ProcessCookOnTheFlyShaders(bool bReloadGlobalShaders, const TArray<uint8>& MeshMaterialMaps, const TArray<FString>& MaterialsToLoad)
{
	check(IsInGameThread());

	// now we need to refresh the RHI resources
	FlushRenderingCommands();

	// reload the global shaders
	if (bReloadGlobalShaders)
	{
		// Some platforms rely on global shaders to be created to implement basic RHI functionality
		TGuardValue<int32> Guard(GCreateShadersOnLoad, 1);
		CompileGlobalShaderMap(true);
	}

	// load all the mesh material shaders if any were sent back
	if (MeshMaterialMaps.Num() > 0)
	{
		// this will stop the rendering thread, and reattach components, in the destructor
		FMaterialUpdateContext UpdateContext;

		// parse the shaders
		FMemoryReader MemoryReader(MeshMaterialMaps, true);
		FNameAsStringProxyArchive Ar(MemoryReader);
		FMaterialShaderMap::LoadForRemoteRecompile(Ar, GMaxRHIShaderPlatform, MaterialsToLoad);

		// gather the shader maps to reattach
		for (TObjectIterator<UMaterial> It; It; ++It)
		{
			UpdateContext.AddMaterial(*It);
		}

		// fixup uniform expressions
		UMaterialInterface::RecacheAllMaterialUniformExpressions(true);

		// Need to recache all cached mesh draw commands, as they store pointers to material uniform buffers which we just invalidated.
		GetRendererModule().UpdateStaticDrawLists();
	}
}

/**
* Forces a recompile of the global shaders.
*/
void RecompileGlobalShaders()
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		// Flush pending accesses to the existing global shaders.
		FlushRenderingCommands();

		UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
			auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
			GetGlobalShaderMap(ShaderPlatform)->Empty();
			VerifyGlobalShaders(ShaderPlatform, nullptr, false);
		});

		GShaderCompilingManager->ProcessAsyncResults(false, true);
	}
}

void GetOutdatedShaderTypes(TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes)
{
#if WITH_EDITOR
	for (int PlatformIndex = 0; PlatformIndex < SP_NumPlatforms; ++PlatformIndex)
	{
		const FGlobalShaderMap* ShaderMap = GGlobalShaderMap[PlatformIndex];
		if (ShaderMap)
		{
			ShaderMap->GetOutdatedTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
		}
	}

	FMaterialShaderMap::GetAllOutdatedTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);

	for (int32 TypeIndex = 0; TypeIndex < OutdatedShaderTypes.Num(); TypeIndex++)
	{
		UE_LOG(LogShaders, Warning, TEXT("		Recompiling %s"), OutdatedShaderTypes[TypeIndex]->GetName());
	}
	for (int32 TypeIndex = 0; TypeIndex < OutdatedShaderPipelineTypes.Num(); TypeIndex++)
	{
		UE_LOG(LogShaders, Warning, TEXT("		Recompiling %s"), OutdatedShaderPipelineTypes[TypeIndex]->GetName());
	}
	for (int32 TypeIndex = 0; TypeIndex < OutdatedFactoryTypes.Num(); TypeIndex++)
	{
		UE_LOG(LogShaders, Warning, TEXT("		Recompiling %s"), OutdatedFactoryTypes[TypeIndex]->GetName());
	}
#endif // WITH_EDITOR
}

bool RecompileShaders(const TCHAR* Cmd, FOutputDevice& Ar)
{
	// if this platform can't compile shaders, then we try to send a message to a file/cooker server
	if (FPlatformProperties::RequiresCookedData())
	{
#if WITH_ODSC
		TArray<FString> MaterialsToLoad;
		bool bCompileChangedShaders = ParseRecompileCommandString(Cmd, MaterialsToLoad);
		GODSCManager->AddThreadedRequest(MaterialsToLoad, GMaxRHIShaderPlatform, bCompileChangedShaders);
#endif
		return true;
	}

	FString FlagStr(FParse::Token(Cmd, 0));
	if( FlagStr.Len() > 0 )
	{
		GWarn->BeginSlowTask( NSLOCTEXT("ShaderCompilingManager", "BeginRecompilingShadersTask", "Recompiling shaders"), true );

		// Flush the shader file cache so that any changes to shader source files will be detected
		FlushShaderFileCache();
		FlushRenderingCommands();

		if( FCString::Stricmp(*FlagStr,TEXT("Changed"))==0)
		{
			TArray<const FShaderType*> OutdatedShaderTypes;
			TArray<const FVertexFactoryType*> OutdatedFactoryTypes;
			TArray<const FShaderPipelineType*> OutdatedShaderPipelineTypes;
			{
				FRecompileShadersTimer SearchTimer(TEXT("Searching for changed files"));
				GetOutdatedShaderTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
			}

			if (OutdatedShaderPipelineTypes.Num() > 0 || OutdatedShaderTypes.Num() > 0 || OutdatedFactoryTypes.Num() > 0)
			{
				FRecompileShadersTimer TestTimer(TEXT("RecompileShaders Changed"));

				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					BeginRecompileGlobalShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, ShaderPlatform);
				});

				// Block on global shaders
				FinishRecompileGlobalShaders();

				// Kick off global shader recompiles
				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					UMaterial::UpdateMaterialShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes, ShaderPlatform);
				});

				GWarn->StatusUpdate(0, 1, NSLOCTEXT("ShaderCompilingManager", "CompilingGlobalShaderStatus", "Compiling global shaders..."));
			}
			else
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("No Shader changes found."));
			}
		}
		else if( FCString::Stricmp(*FlagStr,TEXT("Global"))==0)
		{
			FRecompileShadersTimer TestTimer(TEXT("RecompileShaders Global"));
			RecompileGlobalShaders();
		}
		else if( FCString::Stricmp(*FlagStr,TEXT("Material"))==0)
		{
			FString RequestedMaterialName(FParse::Token(Cmd, 0));
			FRecompileShadersTimer TestTimer(FString::Printf(TEXT("Recompile Material %s"), *RequestedMaterialName));
			bool bMaterialFound = false;
			for( TObjectIterator<UMaterial> It; It; ++It )
			{
				UMaterial* Material = *It;
				if( Material && Material->GetName() == RequestedMaterialName)
				{
					bMaterialFound = true;
#if WITH_EDITOR
					// <Pre/Post>EditChange will force a re-creation of the resource,
					// in turn recompiling the shader.
					Material->PreEditChange(NULL);
					Material->PostEditChange();
#endif // WITH_EDITOR
					break;
				}
			}

			if (!bMaterialFound)
			{
				TestTimer.Stop(false);
				UE_LOG(LogShaderCompilers, Warning, TEXT("Couldn't find Material %s!"), *RequestedMaterialName);
			}
		}
		else if( FCString::Stricmp(*FlagStr,TEXT("All"))==0)
		{
			FRecompileShadersTimer TestTimer(TEXT("RecompileShaders"));
			RecompileGlobalShaders();

			FMaterialUpdateContext UpdateContext;
			for( TObjectIterator<UMaterial> It; It; ++It )
			{
				UMaterial* Material = *It;
				if( Material )
				{
					UE_LOG(LogShaderCompilers, Log, TEXT("recompiling [%s]"),*Material->GetFullName());
					UpdateContext.AddMaterial(Material);
#if WITH_EDITOR
					// <Pre/Post>EditChange will force a re-creation of the resource,
					// in turn recompiling the shader.
					Material->PreEditChange(NULL);
					Material->PostEditChange();
#endif // WITH_EDITOR
				}
			}
		}
		else
		{
			TArray<const FShaderType*> ShaderTypes = FShaderType::GetShaderTypesByFilename(*FlagStr);
			TArray<const FShaderPipelineType*> ShaderPipelineTypes = FShaderPipelineType::GetShaderPipelineTypesByFilename(*FlagStr);
			if (ShaderTypes.Num() > 0 || ShaderPipelineTypes.Num() > 0)
			{
				FRecompileShadersTimer TestTimer(TEXT("RecompileShaders SingleShader"));
				
				TArray<const FVertexFactoryType*> FactoryTypes;

				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					BeginRecompileGlobalShaders(ShaderTypes, ShaderPipelineTypes, ShaderPlatform);
					//UMaterial::UpdateMaterialShaders(ShaderTypes, ShaderPipelineTypes, FactoryTypes, ShaderPlatform);
					FinishRecompileGlobalShaders();
				});
			}
		}

		GWarn->EndSlowTask();

		return 1;
	}

	UE_LOG(LogShaderCompilers, Warning, TEXT("Invalid parameter. Options are: \n'Changed', 'Global', 'Material [name]', 'All' 'Platform [name]'\nNote: Platform implies Changed, and requires the proper target platform modules to be compiled."));
	return 1;
}

static void PrepareGlobalShaderCompileJob(EShaderPlatform Platform,
	EShaderPermutationFlags PermutationFlags,
	const FShaderPipelineType* ShaderPipeline,
	FShaderCompileJob* NewJob)
{
	const FShaderCompileJobKey& Key = NewJob->Key;
	const FGlobalShaderType* ShaderType = Key.ShaderType->AsGlobalShaderType();

	FShaderCompilerEnvironment& ShaderEnvironment = NewJob->Input.Environment;

	UE_LOG(LogShaders, Verbose, TEXT("	%s"), ShaderType->GetName());
	COOK_STAT(GlobalShaderCookStats::ShadersCompiled++);

	// Allow the shader type to modify the compile environment.
	ShaderType->SetupCompileEnvironment(Platform, Key.PermutationId, PermutationFlags, ShaderEnvironment);

	static FString GlobalName(TEXT("Global"));

	// Compile the shader environment passed in with the shader type's source code.
	::GlobalBeginCompileShader(
		GlobalName,
		nullptr,
		ShaderType,
		ShaderPipeline,
		Key.PermutationId,
		ShaderType->GetShaderFilename(),
		ShaderType->GetFunctionName(),
		FShaderTarget(ShaderType->GetFrequency(), Platform),
		NewJob->Input
	);
}

void FGlobalShaderTypeCompiler::BeginCompileShader(const FGlobalShaderType* ShaderType, int32 PermutationId, EShaderPlatform Platform, EShaderPermutationFlags PermutationFlags, TArray<FShaderCommonCompileJobPtr>& NewJobs)
{
	// Global shaders are always high priority (often need to block on completion)
	FShaderCompileJob* NewJob = GShaderCompilingManager->PrepareShaderCompileJob(GlobalShaderMapId, FShaderCompileJobKey(ShaderType, nullptr, PermutationId), EShaderCompileJobPriority::High);
	if (NewJob)
	{
		PrepareGlobalShaderCompileJob(Platform, PermutationFlags, nullptr, NewJob);
		NewJobs.Add(FShaderCommonCompileJobPtr(NewJob));
	}
}

void FGlobalShaderTypeCompiler::BeginCompileShaderPipeline(EShaderPlatform Platform, EShaderPermutationFlags PermutationFlags, const FShaderPipelineType* ShaderPipeline, TArray<FShaderCommonCompileJobPtr>& NewJobs)
{
	check(ShaderPipeline);
	UE_LOG(LogShaders, Verbose, TEXT("	Pipeline: %s"), ShaderPipeline->GetName());

	// Add all the jobs as individual first, then add the dependencies into a pipeline job
	FShaderPipelineCompileJob* NewPipelineJob = GShaderCompilingManager->PreparePipelineCompileJob(GlobalShaderMapId, FShaderPipelineCompileJobKey(ShaderPipeline, nullptr, kUniqueShaderPermutationId), EShaderCompileJobPriority::High);
	if (NewPipelineJob)
	{
		for (FShaderCompileJob* StageJob : NewPipelineJob->StageJobs)
		{
			PrepareGlobalShaderCompileJob(Platform, PermutationFlags, ShaderPipeline, StageJob);
		}
		NewJobs.Add(FShaderCommonCompileJobPtr(NewPipelineJob));
	}
}

FShader* FGlobalShaderTypeCompiler::FinishCompileShader(const FGlobalShaderType* ShaderType, const FShaderCompileJob& CurrentJob, const FShaderPipelineType* ShaderPipelineType)
{
	FShader* Shader = nullptr;
	if (CurrentJob.bSucceeded)
	{
		EShaderPlatform Platform = CurrentJob.Input.Target.GetPlatform();
		FGlobalShaderMapSection* Section = GGlobalShaderMap[Platform]->FindOrAddSection(ShaderType);

		Section->GetResourceCode()->AddShaderCompilerOutput(CurrentJob.Output);

		if (ShaderPipelineType && !ShaderPipelineType->ShouldOptimizeUnusedOutputs(CurrentJob.Input.Target.GetPlatform()))
		{
			// If sharing shaders in this pipeline, remove it from the type/id so it uses the one in the shared shadermap list
			ShaderPipelineType = nullptr;
		}

		// Create the global shader map hash
		FSHAHash GlobalShaderMapHash;
		{
			FSHA1 HashState;
			const TCHAR* GlobalShaderString = TEXT("GlobalShaderMap");
			HashState.UpdateWithString(GlobalShaderString, FCString::Strlen(GlobalShaderString));
			HashState.Final();
			HashState.GetHash(&GlobalShaderMapHash.Hash[0]);
		}

		Shader = ShaderType->ConstructCompiled(FGlobalShaderType::CompiledShaderInitializerType(ShaderType, CurrentJob.Key.PermutationId, CurrentJob.Output, GlobalShaderMapHash, ShaderPipelineType, nullptr));
		CurrentJob.Output.ParameterMap.VerifyBindingsAreComplete(ShaderType->GetName(), CurrentJob.Output.Target, CurrentJob.Key.VFType);
	}

	if (CurrentJob.Output.Errors.Num() > 0)
	{
		if (CurrentJob.bSucceeded == false)
		{
			UE_LOG(LogShaderCompilers, Error, TEXT("Errors compiling global shader %s %s %s:\n"), CurrentJob.Key.ShaderType->GetName(), ShaderPipelineType ? TEXT("ShaderPipeline") : TEXT(""), ShaderPipelineType ? ShaderPipelineType->GetName() : TEXT(""));
			for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
			{
				UE_LOG(LogShaderCompilers, Display, TEXT("%s"), *CurrentJob.Output.Errors[ErrorIndex].GetErrorStringWithLineMarker());
			}
		}
		else if (GShowShaderWarnings)
		{
			UE_LOG(LogShaderCompilers, Warning, TEXT("Warnings compiling global shader %s %s %s:\n"), CurrentJob.Key.ShaderType->GetName(), ShaderPipelineType ? TEXT("ShaderPipeline") : TEXT(""), ShaderPipelineType ? ShaderPipelineType->GetName() : TEXT(""));
			for (int32 ErrorIndex = 0; ErrorIndex < CurrentJob.Output.Errors.Num(); ErrorIndex++)
			{
				UE_LOG(LogShaderCompilers, Display, TEXT("%s"), *CurrentJob.Output.Errors[ErrorIndex].GetErrorStringWithLineMarker());
			}
		}
	}

	return Shader;
}

namespace ShaderCompilerUtil
{
	FOnGlobalShadersCompilation GOnGlobalShdersCompilationDelegate;
}

FOnGlobalShadersCompilation& GetOnGlobalShaderCompilation()
{
	return ShaderCompilerUtil::GOnGlobalShdersCompilationDelegate;
}

/**
* Makes sure all global shaders are loaded and/or compiled for the passed in platform.
* Note: if compilation is needed, this only kicks off the compile.
*
* @param	Platform	Platform to verify global shaders for
*/
void VerifyGlobalShaders(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, bool bLoadedFromCacheFile, const TArray<const FShaderType*>* OutdatedShaderTypes, const TArray<const FShaderPipelineType*>* OutdatedShaderPipelineTypes)
{
	SCOPED_LOADTIMER(VerifyGlobalShaders);

	check(IsInGameThread());
	check(!FPlatformProperties::IsServerOnly());
	check(GGlobalShaderMap[Platform]);

	FPlatformTypeLayoutParameters LayoutParams;
	LayoutParams.InitializeForPlatform(TargetPlatform);
	const EShaderPermutationFlags PermutationFlags = GetShaderPermutationFlags(LayoutParams);

	UE_LOG(LogMaterial, Verbose, TEXT("Verifying Global Shaders for %s"), *LegacyShaderPlatformToShaderFormat(Platform).ToString());

	// Ensure that the global shader map contains all global shader types.
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(Platform);
	const bool bEmptyMap = GlobalShaderMap->IsEmpty();
	if (bEmptyMap)
	{
		UE_LOG(LogShaders, Log, TEXT("	Empty global shader map, recompiling all global shaders"));
	}

	bool bErrorOnMissing = bLoadedFromCacheFile;
	if (FPlatformProperties::RequiresCookedData())
	{
		// We require all shaders to exist on cooked platforms because we can't compile them.
		bErrorOnMissing = true;
	}

	// All jobs, single & pipeline
	TArray<FShaderCommonCompileJobPtr> GlobalShaderJobs;

	// Add the single jobs first
	TMap<TShaderTypePermutation<const FShaderType>, FShaderCompileJob*> SharedShaderJobs;

	for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList()); ShaderTypeIt; ShaderTypeIt.Next())
	{
		FGlobalShaderType* GlobalShaderType = ShaderTypeIt->GetGlobalShaderType();
		if (!GlobalShaderType)
		{
			continue;
		}

		int32 PermutationCountToCompile = 0;
		for (int32 PermutationId = 0; PermutationId < GlobalShaderType->GetPermutationCount(); PermutationId++)
		{
			if (GlobalShaderType->ShouldCompilePermutation(Platform, PermutationId, PermutationFlags)
				&& (!GlobalShaderMap->HasShader(GlobalShaderType, PermutationId) || (OutdatedShaderTypes && OutdatedShaderTypes->Contains(GlobalShaderType))))
			{
				if (bErrorOnMissing)
				{
					UE_LOG(LogShaders, Fatal, TEXT("Missing global shader %s's permutation %i, Please make sure cooking was successful."),
						GlobalShaderType->GetName(), PermutationId);
				}

				if (OutdatedShaderTypes)
				{
					// Remove old shader, if it exists
					GlobalShaderMap->RemoveShaderTypePermutaion(GlobalShaderType, PermutationId);
				}

				// Compile this global shader type.
				FGlobalShaderTypeCompiler::BeginCompileShader(GlobalShaderType, PermutationId, Platform, PermutationFlags, GlobalShaderJobs);
				//TShaderTypePermutation<const FShaderType> ShaderTypePermutation(GlobalShaderType, PermutationId);
				//check(!SharedShaderJobs.Find(ShaderTypePermutation));
				//SharedShaderJobs.Add(ShaderTypePermutation, Job);
				PermutationCountToCompile++;
			}
		}

		ensureMsgf(
			PermutationCountToCompile < 397,	// ToneMapper today (2019-04-17) can go up to 396 permutations
			TEXT("Global shader %s has %i permutation: probably more that it needs."),
			GlobalShaderType->GetName(), PermutationCountToCompile);

		if (!bEmptyMap && PermutationCountToCompile > 0)
		{
			UE_LOG(LogShaders, Log, TEXT("	%s (%i out of %i)"),
				GlobalShaderType->GetName(), PermutationCountToCompile, GlobalShaderType->GetPermutationCount());
		}
	}

	// Now the pipeline jobs; if it's a shareable pipeline, do not add duplicate jobs
	for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList()); ShaderPipelineIt; ShaderPipelineIt.Next())
	{
		const FShaderPipelineType* Pipeline = *ShaderPipelineIt;
		if (Pipeline->IsGlobalTypePipeline())
		{
			if (!GlobalShaderMap->HasShaderPipeline(Pipeline) || (OutdatedShaderPipelineTypes && OutdatedShaderPipelineTypes->Contains(Pipeline)))
			{
				auto& StageTypes = Pipeline->GetStages();

				if (OutdatedShaderPipelineTypes)
				{
					// Remove old pipeline
					GlobalShaderMap->RemoveShaderPipelineType(Pipeline);
				}

				if (bErrorOnMissing)
				{
					UE_LOG(LogShaders, Fatal, TEXT("Missing global shader pipeline %s, Please make sure cooking was successful."), Pipeline->GetName());
				}

				if (!bEmptyMap)
				{
					UE_LOG(LogShaders, Log, TEXT("	%s"), Pipeline->GetName());
				}

				if (Pipeline->ShouldOptimizeUnusedOutputs(Platform))
				{
					// Make a pipeline job with all the stages
					FGlobalShaderTypeCompiler::BeginCompileShaderPipeline(Platform, PermutationFlags, Pipeline, GlobalShaderJobs);
				}
				else
				{
					// If sharing shaders amongst pipelines, add this pipeline as a dependency of an existing individual job
					for (const FShaderType* ShaderType : StageTypes)
					{
						TShaderTypePermutation<const FShaderType> ShaderTypePermutation(ShaderType, kUniqueShaderPermutationId);

						FShaderCompileJob** Job = SharedShaderJobs.Find(ShaderTypePermutation);
						checkf(Job, TEXT("Couldn't find existing shared job for global shader %s on pipeline %s!"), ShaderType->GetName(), Pipeline->GetName());
						auto* SingleJob = (*Job)->GetSingleShaderJob();
						check(SingleJob);
						auto& SharedPipelinesInJob = SingleJob->SharingPipelines.FindOrAdd(nullptr);
						check(!SharedPipelinesInJob.Contains(Pipeline));
						SharedPipelinesInJob.Add(Pipeline);
					}
				}
			}
		}
	}

	if (GlobalShaderJobs.Num() > 0)
	{
		GetOnGlobalShaderCompilation().Broadcast();
		GShaderCompilingManager->SubmitJobs(GlobalShaderJobs, "Globals");

		const bool bAllowAsynchronousGlobalShaderCompiling =
			// OpenGL requires that global shader maps are compiled before attaching
			// primitives to the scene as it must be able to find FNULLPS.
			// TODO_OPENGL: Allow shaders to be compiled asynchronously.
			// Metal also needs this when using RHI thread because it uses TOneColorVS very early in RHIPostInit()
			!IsOpenGLPlatform(GMaxRHIShaderPlatform) && !IsVulkanPlatform(GMaxRHIShaderPlatform) &&
			!IsMetalPlatform(GMaxRHIShaderPlatform) &&
			FDataDrivenShaderPlatformInfo::GetSupportsAsyncPipelineCompilation(GMaxRHIShaderPlatform) &&
			GShaderCompilingManager->AllowAsynchronousShaderCompiling();

		if (!bAllowAsynchronousGlobalShaderCompiling)
		{
			TArray<int32> ShaderMapIds;
			ShaderMapIds.Add(GlobalShaderMapId);

			GShaderCompilingManager->FinishCompilation(TEXT("Global"), ShaderMapIds);
		}
	}
}

void VerifyGlobalShaders(EShaderPlatform Platform, bool bLoadedFromCacheFile, const TArray<const FShaderType*>* OutdatedShaderTypes, const TArray<const FShaderPipelineType*>* OutdatedShaderPipelineTypes)
{
	VerifyGlobalShaders(Platform, nullptr, bLoadedFromCacheFile, OutdatedShaderTypes, OutdatedShaderPipelineTypes);
}

#include "Misc/PreLoadFile.h"
#include "Serialization/LargeMemoryReader.h"
static FPreLoadFile GGlobalShaderPreLoadFile(*(FString(TEXT("../../../Engine")) / TEXT("GlobalShaderCache-SF_") + FPlatformProperties::IniPlatformName() + TEXT(".bin")));

static const ITargetPlatform* GGlobalShaderTargetPlatform[SP_NumPlatforms] = { nullptr };

static FString GetGlobalShaderCacheOverrideFilename(EShaderPlatform Platform)
{
	return FString(TEXT("Engine")) / TEXT("OverrideGlobalShaderCache-") + LegacyShaderPlatformToShaderFormat(Platform).ToString() + TEXT(".bin");
}

static FString GetGlobalShaderCacheFilename(EShaderPlatform Platform)
{
	return FString(TEXT("Engine")) / TEXT("GlobalShaderCache-") + LegacyShaderPlatformToShaderFormat(Platform).ToString() + TEXT(".bin");
}

/** Creates a string key for the derived data cache entry for the global shader map. */
static FString GetGlobalShaderMapKeyString(const FGlobalShaderMapId& ShaderMapId, EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, TArray<FShaderTypeDependency> const& Dependencies)
{
	FName Format = LegacyShaderPlatformToShaderFormat(Platform);
	FString ShaderMapKeyString = Format.ToString() + TEXT("_") + FString(FString::FromInt(GetTargetPlatformManagerRef().ShaderFormatVersion(Format))) + TEXT("_");
	ShaderMapAppendKeyString(Platform, ShaderMapKeyString);
	ShaderMapId.AppendKeyString(ShaderMapKeyString, Dependencies);
	return FDerivedDataCacheInterface::BuildCacheKey(TEXT("GSM"), GLOBALSHADERMAP_DERIVEDDATA_VER, *ShaderMapKeyString);
}

/** Saves the platform's shader map to the DDC. */
static void SaveGlobalShaderMapToDerivedDataCache(EShaderPlatform Platform)
{
	// We've finally built the global shader map, so we can count the miss as we put it in the DDC.
	COOK_STAT(auto Timer = GlobalShaderCookStats::UsageStats.TimeSyncWork());

	const ITargetPlatform* TargetPlatform = GGlobalShaderTargetPlatform[Platform];

	TArray<uint8> SaveData;

	FGlobalShaderMapId ShaderMapId(Platform, TargetPlatform);
	// avoid saving incomplete shadermaps
	FGlobalShaderMap* GlobalSM = GetGlobalShaderMap(Platform);
	if (GlobalSM->IsComplete(TargetPlatform))
	{
		for (auto const& ShaderFilenameDependencies : ShaderMapId.GetShaderFilenameToDependeciesMap())
		{
			FGlobalShaderMapSection* Section = GlobalSM->FindSection(ShaderFilenameDependencies.Key);
			if (Section)
			{
				Section->FinalizeContent();
	

				SaveData.Reset();
				FMemoryWriter Ar(SaveData, true);
				Section->Serialize(Ar);

				GetDerivedDataCacheRef().Put(*GetGlobalShaderMapKeyString(ShaderMapId, Platform, TargetPlatform, ShaderFilenameDependencies.Value), SaveData, TEXT("GlobalShaderMap"_SV));
				COOK_STAT(Timer.AddMiss(SaveData.Num()));
			}
		}
	}
}

/** Saves the global shader map as a file for the target platform. */
FString SaveGlobalShaderFile(EShaderPlatform Platform, FString SavePath, class ITargetPlatform* TargetPlatform)
{
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(Platform);

	// Wait until all global shaders are compiled
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->ProcessAsyncResults(false, true);
	}

	TArray<uint8> GlobalShaderData;
	{
		FMemoryWriter MemoryWriter(GlobalShaderData, true);
		if (TargetPlatform)
		{
			MemoryWriter.SetCookingTarget(TargetPlatform);
		}
		GlobalShaderMap->SaveToGlobalArchive(MemoryWriter);
	}

	// make the final name
	FString FullPath = SavePath / GetGlobalShaderCacheFilename(Platform);
	if (!FFileHelper::SaveArrayToFile(GlobalShaderData, *FullPath))
	{
		UE_LOG(LogShaders, Fatal, TEXT("Could not save global shader file to '%s'"), *FullPath);
	}

#if WITH_EDITOR
	if (FShaderLibraryCooker::NeedsShaderStableKeys(Platform))
	{
		GlobalShaderMap->SaveShaderStableKeys(Platform);
	}
#endif // WITH_EDITOR
	return FullPath;
}


static inline bool ShouldCacheGlobalShaderTypeName(const FGlobalShaderType* GlobalShaderType, int32 PermutationId, const TCHAR* TypeNameSubstring, EShaderPlatform Platform)
{
	return GlobalShaderType
		&& (TypeNameSubstring == nullptr || (FPlatformString::Strstr(GlobalShaderType->GetName(), TypeNameSubstring) != nullptr))
		&& GlobalShaderType->ShouldCompilePermutation(Platform, PermutationId, GetCurrentShaderPermutationFlags());
};


bool IsGlobalShaderMapComplete(const TCHAR* TypeNameSubstring)
{
	for (int32 i = 0; i < SP_NumPlatforms; ++i)
	{
		EShaderPlatform Platform = (EShaderPlatform)i;

		FGlobalShaderMap* GlobalShaderMap = GGlobalShaderMap[Platform];

		if (GlobalShaderMap)
		{
			// Check if the individual shaders are complete
			for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList()); ShaderTypeIt; ShaderTypeIt.Next())
			{
				FGlobalShaderType* GlobalShaderType = ShaderTypeIt->GetGlobalShaderType();
				int32 PermutationCount = GlobalShaderType ? GlobalShaderType->GetPermutationCount() : 1;
				for (int32 PermutationId = 0; PermutationId < PermutationCount; PermutationId++)
				{
					if (ShouldCacheGlobalShaderTypeName(GlobalShaderType, PermutationId, TypeNameSubstring, Platform))
					{
						if (!GlobalShaderMap->HasShader(GlobalShaderType, PermutationId))
						{
							return false;
						}
					}
				}
			}

			// Then the pipelines as it may be sharing shaders
			for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList()); ShaderPipelineIt; ShaderPipelineIt.Next())
			{
				const FShaderPipelineType* Pipeline = *ShaderPipelineIt;
				if (Pipeline->IsGlobalTypePipeline())
				{
					auto& Stages = Pipeline->GetStages();
					int32 NumStagesNeeded = 0;
					for (const FShaderType* Shader : Stages)
					{
						const FGlobalShaderType* GlobalShaderType = Shader->GetGlobalShaderType();
						if (ShouldCacheGlobalShaderTypeName(GlobalShaderType, kUniqueShaderPermutationId, TypeNameSubstring, Platform))
						{
							++NumStagesNeeded;
						}
						else
						{
							break;
						}
					}

					if (NumStagesNeeded == Stages.Num())
					{
						if (!GlobalShaderMap->HasShaderPipeline(Pipeline))
						{
							return false;
						}
					}
				}
			}
		}
	}

	return true;
}

void CompileGlobalShaderMap(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, bool bRefreshShaderMap)
{
	// No global shaders needed on dedicated server or clients that use NullRHI. Note that cook commandlet needs to have them, even if it is not allowed to render otherwise.
	if (FPlatformProperties::IsServerOnly() || (!IsRunningCommandlet() && !FApp::CanEverRender()))
	{
		if (!GGlobalShaderMap[Platform])
		{
			GGlobalShaderMap[Platform] = new FGlobalShaderMap(Platform);
		}
		return;
	}

	if (bRefreshShaderMap || GGlobalShaderTargetPlatform[Platform] != TargetPlatform)
	{
		// delete the current global shader map
		delete GGlobalShaderMap[Platform];
		GGlobalShaderMap[Platform] = nullptr;

		GGlobalShaderTargetPlatform[Platform] = TargetPlatform;

		// make sure we look for updated shader source files
		FlushShaderFileCache();
	}

	// If the global shader map hasn't been created yet, create it.
	if (!GGlobalShaderMap[Platform])
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("GetGlobalShaderMap"), STAT_GetGlobalShaderMap, STATGROUP_LoadTime);
		// GetGlobalShaderMap is called the first time during startup in the main thread.
		check(IsInGameThread());

		FScopedSlowTask SlowTask(70, LOCTEXT("CreateGlobalShaderMap", "Creating Global Shader Map..."));

		// verify that all shader source files are intact
		SlowTask.EnterProgressFrame(20, LOCTEXT("VerifyShaderSourceFiles", "Verifying Global Shader source files..."));
		VerifyShaderSourceFiles(Platform);

		GGlobalShaderMap[Platform] = new FGlobalShaderMap(Platform);

		bool bLoadedFromCacheFile = false;

		// Try to load the global shaders from a local cache file if it exists
		// This method is used exclusively with cooked content, since the DDC is not present
		if (FPlatformProperties::RequiresCookedData())
		{
			SlowTask.EnterProgressFrame(50);

			// Load from the override global shaders first, this allows us to hot reload in cooked / pak builds
			TArray<uint8> GlobalShaderData;
			const bool bAllowOverrideGlobalShaders = !WITH_EDITOR && !UE_BUILD_SHIPPING;
			if (bAllowOverrideGlobalShaders)
			{
				FString OverrideGlobalShaderCacheFilename = GetGlobalShaderCacheOverrideFilename(Platform);
				FPaths::MakeStandardFilename(OverrideGlobalShaderCacheFilename);
				bLoadedFromCacheFile = FFileHelper::LoadFileToArray(GlobalShaderData, *OverrideGlobalShaderCacheFilename, FILEREAD_Silent);
			}

			// is the data already loaded?
			int64 PreloadedSize = 0;
			void* PreloadedData = nullptr;
			if (!bLoadedFromCacheFile)
			{
				PreloadedData = GGlobalShaderPreLoadFile.TakeOwnershipOfLoadedData(&PreloadedSize);
			}

			if (PreloadedData != nullptr)
			{
				FLargeMemoryReader MemoryReader((uint8*)PreloadedData, PreloadedSize, ELargeMemoryReaderFlags::TakeOwnership);
				GGlobalShaderMap[Platform]->LoadFromGlobalArchive(MemoryReader);
			}
			else
			{
				FString GlobalShaderCacheFilename = FPaths::GetRelativePathToRoot() / GetGlobalShaderCacheFilename(Platform);
				FPaths::MakeStandardFilename(GlobalShaderCacheFilename);
				if (!bLoadedFromCacheFile)
				{
					bLoadedFromCacheFile = FFileHelper::LoadFileToArray(GlobalShaderData, *GlobalShaderCacheFilename, FILEREAD_Silent);
				}

				if (!bLoadedFromCacheFile)
				{
					// Handle this gracefully and exit.
					FString SandboxPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*GlobalShaderCacheFilename);
					// This can be too early to localize in some situations.
					const FText Message = FText::Format(NSLOCTEXT("Engine", "GlobalShaderCacheFileMissing", "The global shader cache file '{0}' is missing.\n\nYour application is built to load COOKED content. No COOKED content was found; This usually means you did not cook content for this build.\nIt also may indicate missing cooked data for a shader platform(e.g., OpenGL under Windows): Make sure your platform's packaging settings include this Targeted RHI.\n\nAlternatively build and run the UNCOOKED version instead."), FText::FromString(SandboxPath));
					if (FPlatformProperties::SupportsWindowedMode())
					{
						UE_LOG(LogShaders, Error, TEXT("%s"), *Message.ToString());
						FMessageDialog::Open(EAppMsgType::Ok, Message);
						FPlatformMisc::RequestExit(false);
						return;
					}
					else
					{
						UE_LOG(LogShaders, Fatal, TEXT("%s"), *Message.ToString());
					}
				}

				FMemoryReader MemoryReader(GlobalShaderData);
				GGlobalShaderMap[Platform]->LoadFromGlobalArchive(MemoryReader);
			}
		}
		// Uncooked platform
		else
		{
			FGlobalShaderMapId ShaderMapId(Platform, TargetPlatform);

			const int32 ShaderFilenameNum = ShaderMapId.GetShaderFilenameToDependeciesMap().Num();
			const float ProgressStep = 25.0f / ShaderFilenameNum;

			TArray<uint32> AsyncDDCRequestHandles;
			AsyncDDCRequestHandles.SetNum(ShaderFilenameNum);

			int32 HandleIndex = 0;

			// Submit DDC requests.
			for (const auto& ShaderFilenameDependencies : ShaderMapId.GetShaderFilenameToDependeciesMap())
			{
				SlowTask.EnterProgressFrame(ProgressStep, LOCTEXT("SubmitDDCRequests", "Submitting global shader DDC Requests..."));

				const FString DataKey = GetGlobalShaderMapKeyString(ShaderMapId, Platform, TargetPlatform, ShaderFilenameDependencies.Value);

				AsyncDDCRequestHandles[HandleIndex] = GetDerivedDataCacheRef().GetAsynchronous(*DataKey, TEXT("GlobalShaderMap"_SV));

				++HandleIndex;
			}


			TArray<uint8> CachedData;

			HandleIndex = 0;

			// Process finished DDC requests.
			for (const auto& ShaderFilenameDependencies : ShaderMapId.GetShaderFilenameToDependeciesMap())
			{
				SlowTask.EnterProgressFrame(ProgressStep, LOCTEXT("ProcessDDCRequests", "Processing global shader DDC requests..."));
				CachedData.Reset();
				COOK_STAT(auto Timer = GlobalShaderCookStats::UsageStats.TimeSyncWork());

				GetDerivedDataCacheRef().WaitAsynchronousCompletion(AsyncDDCRequestHandles[HandleIndex]);
				if (GetDerivedDataCacheRef().GetAsynchronousResults(AsyncDDCRequestHandles[HandleIndex], CachedData))
				{
					COOK_STAT(Timer.AddHit(CachedData.Num()));
					FMemoryReader MemoryReader(CachedData);
					GGlobalShaderMap[Platform]->AddSection(FGlobalShaderMapSection::CreateFromArchive(MemoryReader));
				}
				else
				{
					// it's a miss, but we haven't built anything yet. Save the counting until we actually have it built.
					COOK_STAT(Timer.TrackCyclesOnly());
				}

				++HandleIndex;
			}
		}

		// If any shaders weren't loaded, compile them now.
		VerifyGlobalShaders(Platform, TargetPlatform, bLoadedFromCacheFile);

		if (GCreateShadersOnLoad && Platform == GMaxRHIShaderPlatform)
		{
			GGlobalShaderMap[Platform]->BeginCreateAllShaders();
		}
	}
}

void CompileGlobalShaderMap(EShaderPlatform Platform, bool bRefreshShaderMap)
{
	CompileGlobalShaderMap(Platform, nullptr, bRefreshShaderMap);
}

void CompileGlobalShaderMap(ERHIFeatureLevel::Type InFeatureLevel, bool bRefreshShaderMap)
{
	EShaderPlatform Platform = GShaderPlatformForFeatureLevel[InFeatureLevel];
	CompileGlobalShaderMap(Platform, nullptr, bRefreshShaderMap);
}

void CompileGlobalShaderMap(bool bRefreshShaderMap)
{
	CompileGlobalShaderMap(GMaxRHIFeatureLevel, bRefreshShaderMap);
}

void ReloadGlobalShaders()
{
	// Flush pending accesses to the existing global shaders.
	FlushRenderingCommands();

	UMaterialInterface::IterateOverActiveFeatureLevels(
		[&](ERHIFeatureLevel::Type InFeatureLevel)
		{
			auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
			GetGlobalShaderMap(ShaderPlatform)->ReleaseAllSections();
			CompileGlobalShaderMap(InFeatureLevel, true);
			VerifyGlobalShaders(ShaderPlatform, nullptr, false);
		}
	);

	// Invalidate global bound shader states so they will be created with the new shaders the next time they are set (in SetGlobalBoundShaderState)
	for (TLinkedList<FGlobalBoundShaderStateResource*>::TIterator It(FGlobalBoundShaderStateResource::GetGlobalBoundShaderStateList()); It; It.Next())
	{
		BeginUpdateResourceRHI(*It);
	}
}

static FAutoConsoleCommand CCmdReloadGlobalShaders = FAutoConsoleCommand(
	TEXT("ReloadGlobalShaders"),
	TEXT("Reloads the global shaders file"),
	FConsoleCommandDelegate::CreateStatic(ReloadGlobalShaders)
);

bool RecompileChangedShadersForPlatform(const FString& PlatformName)
{
	// figure out what shader platforms to recompile
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	ITargetPlatform* TargetPlatform = TPM->FindTargetPlatform(PlatformName);
	if (TargetPlatform == NULL)
	{
		UE_LOG(LogShaders, Display, TEXT("Failed to find target platform module for %s"), *PlatformName);
		return false;
	}

	TArray<FName> DesiredShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);



	// figure out which shaders are out of date
	TArray<const FShaderType*> OutdatedShaderTypes;
	TArray<const FVertexFactoryType*> OutdatedFactoryTypes;
	TArray<const FShaderPipelineType*> OutdatedShaderPipelineTypes;

	// Pick up new changes to shader files
	FlushShaderFileCache();

	GetOutdatedShaderTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
	UE_LOG(LogShaders, Display, TEXT("We found %d out of date shader types, %d outdated pipeline types, and %d out of date VF types!"), OutdatedShaderTypes.Num(), OutdatedShaderPipelineTypes.Num(), OutdatedFactoryTypes.Num());

	for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
	{
		// get the shader platform enum
		const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);

		// Only compile for the desired platform if requested
		// Kick off global shader recompiles
		BeginRecompileGlobalShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, ShaderPlatform);

		// Block on global shaders
		FinishRecompileGlobalShaders();
#if WITH_EDITOR
		// we only want to actually compile mesh shaders if we have out of date ones
		if (OutdatedShaderTypes.Num() || OutdatedFactoryTypes.Num())
		{
			for (TObjectIterator<UMaterialInterface> It; It; ++It)
			{
				(*It)->ClearCachedCookedPlatformData(TargetPlatform);
			}
		}
#endif
	}

	if (OutdatedFactoryTypes.Num() || OutdatedShaderTypes.Num())
	{
		return true;
	}
	return false;
}

FArchive& operator<<(FArchive& Ar, FODSCRequestPayload& Elem)
{
	uint32 ConvertedShaderPlatform = (uint32)Elem.ShaderPlatform;
	Ar << ConvertedShaderPlatform;
	Ar << Elem.MaterialName;
	Ar << Elem.VertexFactoryName;
	Ar << Elem.PipelineName;
	Ar << Elem.ShaderTypeNames;
	Ar << Elem.RequestHash;

	return Ar;
}

void RecompileShadersForRemote(
	const FString& PlatformName,
	EShaderPlatform ShaderPlatformToCompile,
	const FString& OutputDirectory,
	const TArray<FString>& MaterialsToLoad,
	const TArray<FODSCRequestPayload>& ShadersToRecompile,
	TArray<uint8>* MeshMaterialMaps,
	TArray<FString>* ModifiedFiles,
	bool bCompileChangedShaders)
{
	// figure out what shader platforms to recompile
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	ITargetPlatform* TargetPlatform = TPM->FindTargetPlatform(PlatformName);
	if (TargetPlatform == NULL)
	{
		UE_LOG(LogShaders, Display, TEXT("Failed to find target platform module for %s"), *PlatformName);
		return;
	}

	const bool bPreviousState = GShaderCompilingManager->IsShaderCompilationSkipped();
	GShaderCompilingManager->SkipShaderCompilation(false);

	TArray<FName> DesiredShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

	UE_LOG(LogShaders, Display, TEXT("Loading %d materials..."), MaterialsToLoad.Num());
	// make sure all materials the client has loaded will be processed
	TArray<UMaterialInterface*> MaterialsToCompile;

	for (int32 Index = 0; Index < MaterialsToLoad.Num(); Index++)
	{
		UE_LOG(LogShaders, Display, TEXT("   --> %s"), *MaterialsToLoad[Index]);
		MaterialsToCompile.Add(LoadObject<UMaterialInterface>(NULL, *MaterialsToLoad[Index]));
	}

	UE_LOG(LogShaders, Display, TEXT("  Done!"))

	// figure out which shaders are out of date
	TArray<const FShaderType*> OutdatedShaderTypes;
	TArray<const FVertexFactoryType*> OutdatedFactoryTypes;
	TArray<const FShaderPipelineType*> OutdatedShaderPipelineTypes;

	// Pick up new changes to shader files
	FlushShaderFileCache();

	if (bCompileChangedShaders)
	{
		GetOutdatedShaderTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
		UE_LOG(LogShaders, Display, TEXT("We found %d out of date shader types, %d outdated pipeline types, and %d out of date VF types!"), OutdatedShaderTypes.Num(), OutdatedShaderPipelineTypes.Num(), OutdatedFactoryTypes.Num());
	}

	if (ShadersToRecompile.Num())
	{
		UE_LOG(LogShaders, Display, TEXT("Received %d shaders to compile."), ShadersToRecompile.Num());
	}

	for (const FODSCRequestPayload& payload: ShadersToRecompile)
	{
		UE_LOG(LogShaders, Display, TEXT("Material: %s "), *payload.MaterialName);
		UE_LOG(LogShaders, Display, TEXT("VFType: %s "), *payload.VertexFactoryName);
		UE_LOG(LogShaders, Display, TEXT("Pipeline: %s "), *payload.PipelineName);

		MaterialsToCompile.Add(LoadObject<UMaterialInterface>(NULL, *payload.MaterialName));

		const FVertexFactoryType* VFType = FVertexFactoryType::GetVFByName(payload.VertexFactoryName);
		if (VFType)
		{
			OutdatedFactoryTypes.Add(VFType);
		}

		const FShaderPipelineType* PipelineType = FShaderPipelineType::GetShaderPipelineTypeByName(payload.PipelineName);
		if (PipelineType)
		{
			OutdatedShaderPipelineTypes.Add(PipelineType);
		}

		for (const FString& ShaderTypeName : payload.ShaderTypeNames)
		{
			UE_LOG(LogShaders, Display, TEXT("\tShaderType: %s"), *ShaderTypeName);

			const FShaderType* ShaderType = FShaderType::GetShaderTypeByName(*ShaderTypeName);
			if (ShaderType)
			{
				OutdatedShaderTypes.Add(ShaderType);
			}
		}
	}

	{
		for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
		{
			// get the shader platform enum
			const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);

			// Only compile for the desired platform if requested
			if (ShaderPlatform == ShaderPlatformToCompile || ShaderPlatformToCompile == SP_NumPlatforms)
			{
				if (bCompileChangedShaders)
				{
					// Kick off global shader recompiles
					BeginRecompileGlobalShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, ShaderPlatform, TargetPlatform);

					// Block on global shaders
					FinishRecompileGlobalShaders();
				}

				// we only want to actually compile mesh shaders if a client directly requested it, and there's actually some work to do
				if (MeshMaterialMaps != NULL && (OutdatedShaderTypes.Num() || OutdatedFactoryTypes.Num() || bCompileChangedShaders == false))
				{
					TMap<FString, TArray<TRefCountPtr<FMaterialShaderMap> > > CompiledShaderMaps;
					UMaterial::CompileMaterialsForRemoteRecompile(MaterialsToCompile, ShaderPlatform, TargetPlatform, CompiledShaderMaps);

					// write the shader compilation info to memory, converting fnames to strings
					FMemoryWriter MemWriter(*MeshMaterialMaps, true);
					FNameAsStringProxyArchive Ar(MemWriter);
					Ar.SetCookingTarget(TargetPlatform);

					// save out the shader map to the byte array
					FMaterialShaderMap::SaveForRemoteRecompile(Ar, CompiledShaderMaps);
				}

				// save it out so the client can get it (and it's up to date next time)
				FString GlobalShaderFilename = SaveGlobalShaderFile(ShaderPlatform, OutputDirectory, TargetPlatform);

				// add this to the list of files to tell the other end about
				if (ModifiedFiles)
				{
					// need to put it in non-sandbox terms
					FString SandboxPath(GlobalShaderFilename);
					check(SandboxPath.StartsWith(OutputDirectory));
					SandboxPath.ReplaceInline(*OutputDirectory, TEXT("../../../"));
					FPaths::NormalizeFilename(SandboxPath);
					ModifiedFiles->Add(SandboxPath);
				}
			}
		}
	}

	// Restore compilation state.
	GShaderCompilingManager->SkipShaderCompilation(bPreviousState);
}

void BeginRecompileGlobalShaders(const TArray<const FShaderType*>& OutdatedShaderTypes, const TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, EShaderPlatform ShaderPlatform, const ITargetPlatform* TargetPlatform)
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		// Flush pending accesses to the existing global shaders.
		FlushRenderingCommands();

		// Calling CompileGlobalShaderMap will force starting the compile jobs if the map is empty (by calling VerifyGlobalShaders)
		CompileGlobalShaderMap(ShaderPlatform, TargetPlatform, false);
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ShaderPlatform);

		// Now check if there is any work to be done wrt outdates types
		if (OutdatedShaderTypes.Num() > 0 || OutdatedShaderPipelineTypes.Num() > 0)
		{

			VerifyGlobalShaders(ShaderPlatform, TargetPlatform, false, &OutdatedShaderTypes, &OutdatedShaderPipelineTypes);
		}
	}
}

void FinishRecompileGlobalShaders()
{
	// Block until global shaders have been compiled and processed
	GShaderCompilingManager->ProcessAsyncResults(false, true);
}

static inline FShader* ProcessCompiledJob(FShaderCompileJob* SingleJob, const FShaderPipelineType* Pipeline, TArray<EShaderPlatform>& ShaderPlatformsProcessed, TArray<const FShaderPipelineType*>& OutSharedPipelines)
{
	const FGlobalShaderType* GlobalShaderType = SingleJob->Key.ShaderType->GetGlobalShaderType();
	check(GlobalShaderType);
	FShader* Shader = FGlobalShaderTypeCompiler::FinishCompileShader(GlobalShaderType, *SingleJob, Pipeline);
	if (Shader)
	{
		// Add the new global shader instance to the global shader map if it's a shared shader
		EShaderPlatform Platform = (EShaderPlatform)SingleJob->Input.Target.Platform;
		if (!Pipeline || !Pipeline->ShouldOptimizeUnusedOutputs(Platform))
		{
			Shader = GGlobalShaderMap[Platform]->FindOrAddShader(GlobalShaderType, SingleJob->Key.PermutationId, Shader);
			// Add this shared pipeline to the list
			if (!Pipeline)
			{
				auto* JobSharedPipelines = SingleJob->SharingPipelines.Find(nullptr);
				if (JobSharedPipelines)
				{
					for (auto* SharedPipeline : *JobSharedPipelines)
					{
						OutSharedPipelines.AddUnique(SharedPipeline);
					}
				}
			}
		}
		ShaderPlatformsProcessed.AddUnique(Platform);
	}
	else
	{
		UE_LOG(LogShaders, Fatal, TEXT("Failed to compile global shader %s %s %s.  Enable 'r.ShaderDevelopmentMode' in ConsoleVariables.ini for retries."),
			GlobalShaderType->GetName(),
			Pipeline ? TEXT("for pipeline") : TEXT(""),
			Pipeline ? Pipeline->GetName() : TEXT(""));
	}

	return Shader;
};

void ProcessCompiledGlobalShaders(const TArray<FShaderCommonCompileJobPtr>& CompilationResults)
{
	UE_LOG(LogShaders, Verbose, TEXT("Compiled %u global shaders"), CompilationResults.Num());

	TArray<EShaderPlatform> ShaderPlatformsProcessed;
	TArray<const FShaderPipelineType*> SharedPipelines;

	for (int32 ResultIndex = 0; ResultIndex < CompilationResults.Num(); ResultIndex++)
	{
		const FShaderCommonCompileJob& CurrentJob = *CompilationResults[ResultIndex];
		FShaderCompileJob* SingleJob = nullptr;
		if ((SingleJob = (FShaderCompileJob*)CurrentJob.GetSingleShaderJob()) != nullptr)
		{
			ProcessCompiledJob(SingleJob, nullptr, ShaderPlatformsProcessed, SharedPipelines);
		}
		else
		{
			const auto* PipelineJob = CurrentJob.GetShaderPipelineJob();
			check(PipelineJob);

			FShaderPipeline* ShaderPipeline = new FShaderPipeline(PipelineJob->Key.ShaderPipeline);
			for (int32 Index = 0; Index < PipelineJob->StageJobs.Num(); ++Index)
			{
				SingleJob = PipelineJob->StageJobs[Index]->GetSingleShaderJob();
				FShader* Shader = ProcessCompiledJob(SingleJob, PipelineJob->Key.ShaderPipeline, ShaderPlatformsProcessed, SharedPipelines);
				ShaderPipeline->AddShader(Shader, SingleJob->Key.PermutationId);
			}
			ShaderPipeline->Validate(PipelineJob->Key.ShaderPipeline);

			EShaderPlatform Platform = (EShaderPlatform)PipelineJob->StageJobs[0]->GetSingleShaderJob()->Input.Target.Platform;
			check(ShaderPipeline && !GGlobalShaderMap[Platform]->HasShaderPipeline(PipelineJob->Key.ShaderPipeline));
			GGlobalShaderMap[Platform]->FindOrAddShaderPipeline(PipelineJob->Key.ShaderPipeline, ShaderPipeline);
		}
	}

	for (int32 PlatformIndex = 0; PlatformIndex < ShaderPlatformsProcessed.Num(); PlatformIndex++)
	{
		{
			// Process the shader pipelines that share shaders
			EShaderPlatform Platform = ShaderPlatformsProcessed[PlatformIndex];
			FGlobalShaderMap* GlobalShaderMap = GGlobalShaderMap[Platform];
			const ITargetPlatform* TargetPlatform = GGlobalShaderTargetPlatform[Platform];

			FPlatformTypeLayoutParameters LayoutParams;
			LayoutParams.InitializeForPlatform(TargetPlatform);
			const EShaderPermutationFlags PermutationFlags = GetShaderPermutationFlags(LayoutParams);

			for (const FShaderPipelineType* ShaderPipelineType : SharedPipelines)
			{
				check(ShaderPipelineType->IsGlobalTypePipeline());
				if (!GlobalShaderMap->HasShaderPipeline(ShaderPipelineType))
				{
					auto& StageTypes = ShaderPipelineType->GetStages();

					FShaderPipeline* ShaderPipeline = new FShaderPipeline(ShaderPipelineType);
					for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
					{
						FGlobalShaderType* GlobalShaderType = ((FShaderType*)(StageTypes[Index]))->GetGlobalShaderType();
						if (GlobalShaderType->ShouldCompilePermutation(Platform, kUniqueShaderPermutationId, PermutationFlags))
						{
							TShaderRef<FShader> Shader = GlobalShaderMap->GetShader(GlobalShaderType, kUniqueShaderPermutationId);
							check(Shader.IsValid());
							ShaderPipeline->AddShader(Shader.GetShader(), kUniqueShaderPermutationId);
						}
						else
						{
							break;
						}
					}
					ShaderPipeline->Validate(ShaderPipelineType);
					GlobalShaderMap->FindOrAddShaderPipeline(ShaderPipelineType, ShaderPipeline);
				}
			}
		}

		// Save the global shader map for any platforms that were recompiled
		SaveGlobalShaderMapToDerivedDataCache(ShaderPlatformsProcessed[PlatformIndex]);
	}
}

FSHAHash FShaderCompileJob::GetInputHash()
{
	if (bInputHashSet)
	{
		return InputHash;
	}

	auto SerializeInputs = [this](FArchive& Archive)
	{
		checkf(Archive.IsSaving() && !Archive.IsLoading(), TEXT("A loading archive is passed to FShaderCompileJob::GetInputHash(), this is not supported as it may corrupt its data"));

		Archive << Input;
		Archive << Input.Environment;

		// hash the source file so changes to files during the development are picked up
		const FSHAHash &SourceHash = GetShaderFileHash(*Input.VirtualSourceFilePath, Input.Target.GetPlatform());
		Archive << const_cast<FSHAHash &>(SourceHash);

		for (TMap<FString, FThreadSafeSharedStringPtr>::TConstIterator It(Input.Environment.IncludeVirtualPathToExternalContentsMap); It; ++It)
		{
			const FString& VirtualPath = It.Key();
			Archive << const_cast<FString&>(VirtualPath);
			check(It.Value());
			FString& Contents = *It.Value();
			Archive << Contents;
		}

		if (Input.SharedEnvironment)
		{
			Archive << *Input.SharedEnvironment;
			for (TMap<FString, FThreadSafeSharedStringPtr>::TConstIterator It(Input.SharedEnvironment->IncludeVirtualPathToExternalContentsMap); It; ++It)
			{
				const FString& VirtualPath = It.Key();
				Archive << const_cast<FString&>(VirtualPath);
				check(It.Value());
				FString& Contents = *It.Value();
				Archive << Contents;
			}
		}
	};

	// use faster hasher that doesn't allocate memory
	FMemoryHasherSHA1 MemHasher;
	SerializeInputs(MemHasher);
	MemHasher.Finalize();
	InputHash = MemHasher.GetHash();

	if (GShaderCompilerDumpCompileJobInputs)
	{
		TArray<uint8> MemoryBlob;
		FMemoryWriter MemWriter(MemoryBlob);

		SerializeInputs(MemWriter);

		FString IntermediateFormatPath = FPaths::ProjectSavedDir() / TEXT("ShaderJobInputs");
#if UE_BUILD_DEBUG
		FString TempPath = IntermediateFormatPath / TEXT("DebugEditor");
#else
		FString TempPath = IntermediateFormatPath / TEXT("DevelopmentEditor");
#endif
		IFileManager::Get().MakeDirectory(*TempPath, true);

		static int32 InputHashID = 0;
		FString FileName = Input.DebugGroupName.Replace(TEXT("/"), TEXT("_")).Replace(TEXT("<"), TEXT("_")).Replace(TEXT(">"), TEXT("_")).Replace(TEXT(":"), TEXT("_")).Replace(TEXT("|"), TEXT("_"))
			+ TEXT("-") + Input.EntryPointName;
		FString TempFile = TempPath / FString::Printf(TEXT("%s-%d.bin"), *FileName, InputHashID++);

		TUniquePtr<FArchive> DumpAr(IFileManager::Get().CreateFileWriter(*TempFile));
		DumpAr->Serialize(MemoryBlob.GetData(), MemoryBlob.Num());

		// as an additional debugging feature, make sure that the hash is the same as calculated by the memhasher
		FSHAHash Check;
		FSHA1::HashBuffer(MemoryBlob.GetData(), MemoryBlob.Num(), Check.Hash);
		if (Check != InputHash)
		{
			UE_LOG(LogShaderCompilers, Error, TEXT("Job input hash disagrees between FMemoryHasherSHA1 (%s) and FMemoryWriter + FSHA1 (%s, which was dumped to disk)"), *InputHash.ToString(), *Check.ToString());
		}
	}

	bInputHashSet = true;
	return InputHash;
}

void FShaderCompileJob::SerializeOutput(FArchive& Ar)
{
	double ActualCompileTime = 0.0;
	if (Ar.IsSaving())
	{
		// Cached jobs won't have accurate results anyway, so reduce the storage requirements by setting those fields to a known value.
		// This significantly reduces the memory needed to store the outputs (by more than a half)
		ActualCompileTime = Output.CompileTime;
		Output.CompileTime = 0.0;
	}

	Ar << Output;

	if (Ar.IsLoading())
	{
		bFinalized = true;

		// serialize the hash as well? minor optimization
		Output.GenerateOutputHash();
		bSucceeded = Output.bSucceeded;
	}
	else
	{
		// restore the compile time for this jobs. Jobs that will be deserialized from the cache will have a compile time of 0.0
		Output.CompileTime = ActualCompileTime;
	}
}

FSHAHash FShaderPipelineCompileJob::GetInputHash()
{
	if (bInputHashSet)
	{
		return InputHash;
	}

	FSHA1 Hasher;

	for (int32 Index = 0; Index < StageJobs.Num(); ++Index)
	{
		if (StageJobs[Index])
		{
			FSHAHash StageHash = StageJobs[Index]->GetInputHash();
			Hasher.Update(&StageHash.Hash[0], sizeof(StageHash.Hash));
		}
	}

	Hasher.Final();
	Hasher.GetHash(&InputHash.Hash[0]);

	bInputHashSet = true;
	return InputHash;
}

void FShaderPipelineCompileJob::SerializeOutput(FArchive& Ar)
{
	bool bAllStagesSucceeded = true;
	for (int32 Index = 0, Num = StageJobs.Num(); Index < Num; ++Index)
	{
		StageJobs[Index]->SerializeOutput(Ar);
		bAllStagesSucceeded = bAllStagesSucceeded && StageJobs[Index]->bSucceeded;
	}

	if (Ar.IsLoading())
	{
		bFinalized = true;
		bSucceeded = bAllStagesSucceeded;
	}
}

FShaderJobCache::FJobCachedOutput* FShaderJobCache::Find(const FJobInputHash& Hash)
{
	++TotalSearchAttempts;

	if (ShaderCompiler::IsJobCacheEnabled())
	{
		FJobOutputHash* OutputHash = InputHashToOutput.Find(Hash);
		if (OutputHash)
		{
			++TotalCacheHits;

			FStoredOutput** CannedOutput = Outputs.Find(*OutputHash);
			// we should not allow a dangling input to output mapping to exist
			checkf(CannedOutput != nullptr, TEXT("Inconsistency in FShaderJobCache - cache record for ihash %s exists, but output cannot be found."), *Hash.ToString());
			// update the output hit count
			(*CannedOutput)->NumHits++;
			return &(*CannedOutput)->JobOutput;
		}
	}

	return nullptr;
}

uint64 FShaderJobCache::GetCurrentMemoryBudget() const
{
	uint64 AbsoluteLimit = static_cast<uint64>(GShaderCompilerMaxJobCacheMemoryMB) * 1024ULL * 1024ULL;
	uint64 RelativeLimit = FMath::Clamp(static_cast<double>(GShaderCompilerMaxJobCacheMemoryPercent), 0.0, 100.0) * (static_cast<double>(FPlatformMemory::GetPhysicalGBRam()) * 1024 * 1024 * 1024) / 100.0;
	return FMath::Min(AbsoluteLimit, RelativeLimit);
}

void FShaderJobCache::Add(const FJobInputHash& Hash, const FJobCachedOutput& Contents, int32 InitialHitCount)
{
	if (!ShaderCompiler::IsJobCacheEnabled())
	{
		return;
	}

	FJobOutputHash* ExistingOutputHash = InputHashToOutput.Find(Hash);
	if (ExistingOutputHash)
	{
		// we can arrive here due to cloned jobs ignoring our normal caching rules
		return;
	}

	FSHAHash OutputHash;
	FSHA1::HashBuffer(Contents.GetData(), Contents.Num(), OutputHash.Hash);

	// add the record
	InputHashToOutput.Add(Hash, OutputHash);

	FStoredOutput** CannedOutput = Outputs.Find(OutputHash);
	if (CannedOutput)
	{
		// update the output hit count
		(*CannedOutput)->NumReferences++;
	}
	else
	{
		// delete the previous cache entries if we have a budget
		uint64 MemoryBudgetBytes = GetCurrentMemoryBudget();
		if (MemoryBudgetBytes)
		{
			uint64 MemoryThatWillBeUsed = GetAllocatedMemory() + Contents.Num();
			while (MemoryThatWillBeUsed >= MemoryBudgetBytes)
			{
				// heuristics: delete the entry that has the smallest hits. Don't account for references as if something is referenced often but not hit, it's of no value for us.
				// (consider other heuristics: hits * memory, time it took to produce the output, last hit time)
				int32 MinHits = INT_MAX;

				// find the (new) min
				for (TMap<FJobOutputHash, FStoredOutput*>::TConstIterator Iter(Outputs); Iter; ++Iter)
				{
					MinHits = FMath::Min(Iter.Value()->NumHits, MinHits);
				}

				// remove all matching this minimum until there's enough memory
				for (TMap<FJobOutputHash, FStoredOutput*>::TIterator Iter(Outputs); Iter; ++Iter)
				{
					if (Iter.Value()->NumHits == MinHits)
					{
						MemoryThatWillBeUsed -= static_cast<uint64>(Iter.Value()->JobOutput.Num());

						FSHAHash RemovedOutputHash = Iter.Key();
						Iter.RemoveCurrent();

						// remove all mappings
						for (TMap<FJobInputHash, FJobOutputHash>::TIterator IterInputs(InputHashToOutput); IterInputs; ++IterInputs)
						{
							if (IterInputs.Value() == RemovedOutputHash)
							{
								IterInputs.RemoveCurrent();
							}
						}

						// don't remove too much
						if (MemoryThatWillBeUsed < MemoryBudgetBytes)
						{
							break;
						}
					}
				}
			}
		}

		FStoredOutput* NewStoredOutput = new FStoredOutput();
		NewStoredOutput->NumHits = InitialHitCount;
		NewStoredOutput->NumReferences = 1;
		NewStoredOutput->JobOutput = Contents;
		Outputs.Add(OutputHash, NewStoredOutput);

		// invalidate currently allocated memory only if we added something substantial. We ignore memory increase due to TMap size
		CurrentlyAllocatedMemory = 0;
	}
}

/** Calculates memory used by the cache*/
uint64 FShaderJobCache::GetAllocatedMemory()
{
	if (!CurrentlyAllocatedMemory)
	{
		uint64 MemoryUsed = sizeof(*this) + InputHashToOutput.GetAllocatedSize() + Outputs.GetAllocatedSize();

		// go through all the outputs and sum them
		for (TMap<FJobOutputHash, FStoredOutput*>::TConstIterator Iter(Outputs); Iter; ++Iter)
		{
			MemoryUsed += Iter->Value->JobOutput.GetAllocatedSize();
		}

		CurrentlyAllocatedMemory = MemoryUsed;
	}

	return CurrentlyAllocatedMemory;
}

#include "Math/UnitConversion.h"

/** Logs out the statistics */
void FShaderJobCache::LogStats()
{
	UE_LOG(LogShaderCompilers, Display, TEXT("=== FShaderJobCache stats ==="), this);
	UE_LOG(LogShaderCompilers, Display, TEXT("Total job queries %lld, among them cache hits %lld (%.2f%%)"),
		TotalSearchAttempts, TotalCacheHits, (TotalSearchAttempts > 0) ? 100.0 * static_cast<double>(TotalCacheHits) / static_cast<double>(TotalSearchAttempts) : 0.0);
	UE_LOG(LogShaderCompilers, Display, TEXT("Tracking %d distinct input hashes that result in %d distinct outputs (%.2f%%)"),
		InputHashToOutput.Num(), Outputs.Num(), (InputHashToOutput.Num() > 0) ? 100.0 * static_cast<double>(Outputs.Num()) / static_cast<double>(InputHashToOutput.Num()) : 0.0);

	CurrentlyAllocatedMemory = 0;	// get accurate data by invalidating cache
	uint64 MemUsed = GetAllocatedMemory();
	double MemUsedMB = FUnitConversion::Convert(static_cast<double>(MemUsed), EUnit::Bytes, EUnit::Megabytes);
	double MemUsedGB = FUnitConversion::Convert(static_cast<double>(MemUsed), EUnit::Bytes, EUnit::Gigabytes);
	uint64 MemBudget = GetCurrentMemoryBudget();
	if (MemBudget > 0)
	{
		double MemBudgetMB = FUnitConversion::Convert(static_cast<double>(MemBudget), EUnit::Bytes, EUnit::Megabytes);
		double MemBudgetGB = FUnitConversion::Convert(static_cast<double>(MemBudget), EUnit::Bytes, EUnit::Gigabytes);

		UE_LOG(LogShaderCompilers, Display, TEXT("RAM used: %.2f MB (%.2f GB) of %.2f MB (%.2f GB) budget. Usage: %.2f%%"), 
			MemUsedMB, MemUsedGB, MemBudgetMB, MemBudgetGB, 100.0 * MemUsedMB / MemBudgetMB);
	}
	else
	{
		UE_LOG(LogShaderCompilers, Display, TEXT("RAM used: %.2f MB (%.2f GB), no memory limit set"), MemUsedMB, MemUsedGB);
	}
	UE_LOG(LogShaderCompilers, Display, TEXT("================================================"));
}

#undef LOCTEXT_NAMESPACE
