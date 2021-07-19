// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildJob.h"

#include "Algo/Find.h"
#include "Compression/CompressedBuffer.h"
#include "DerivedDataBuild.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildFunctionRegistry.h"
#include "DerivedDataBuildInputResolver.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildJobContext.h"
#include "DerivedDataBuildKey.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPolicy.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataBuildScheduler.h"
#include "DerivedDataBuildWorker.h"
#include "DerivedDataBuildWorkerRegistry.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataPayload.h"
#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMath.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "String/ParseTokens.h"
#include "Templates/Function.h"
#include "Templates/RefCounting.h"
#include <atomic>

namespace UE::DerivedData::Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A build job proceeds through these states in order and may skip states. */
enum class EBuildJobState : uint32
{
	/** A new job that has not been scheduled. */
	NotStarted,
	/** Resolve the key to a definition. */
	ResolveKey,
	/** Wait for the input resolver to resolve the key. */
	ResolveKeyWait,
	/** Resolve each input to its raw hash and raw size. */
	ResolveInputMeta,
	/** Wait for the input resolver to resolve the metadata. */
	ResolveInputMetaWait,
	/** Query the cache for the action. */
	CacheQuery,
	/** Wait for the cache query to finish. */
	CacheQueryWait,
	/** Try to execute remotely without loading additional inputs. */
	ExecuteRemote,
	/** Wait for the remote execution request to finish. */
	ExecuteRemoteWait,
	/** Resolve each input that was missing from remote execution. */
	ResolveRemoteInputData,
	/** Wait for the input resolver to resolve the data. */
	ResolveRemoteInputDataWait,
	/** Try to execute remotely after resolving missing inputs. */
	ExecuteRemoteRetry,
	/** Wait for the remote execution request to finish. */
	ExecuteRemoteRetryWait,
	/** Resolve each input that is not yet resolved. */
	ResolveInputData,
	/** Wait for the input resolver to resolve the data. */
	ResolveInputDataWait,
	/** Execute the function locally to build the output. */
	ExecuteLocal,
	/** Wait for the async function to finish executing locally. */
	ExecuteLocalWait,
	/** Store the output in the cache. */
	CacheStore,
	/** Wait for the cache store to finish. */
	CacheStoreWait,
	/** Complete. */
	Complete,
};

class FBuildJob final : public IBuildJob
{
public:
	/** Resolve the key to a definition, then build like the definition constructor. */
	FBuildJob(ICache& Cache, IBuild& BuildSystem, IBuildInputResolver* InputResolver, const FBuildKey& Key);
	/** Resolve the definition to an action, then build like the action constructor. */
	FBuildJob(ICache& Cache, IBuild& BuildSystem, IBuildInputResolver* InputResolver, const FBuildDefinition& Definition);
	/** Query the cache, attempt remote execution, resolve inputs, fall back to local execution, store to the cache. */
	FBuildJob(ICache& Cache, IBuild& BuildSystem, IBuildInputResolver* InputResolver, const FBuildAction& Action, const FOptionalBuildInputs& Inputs);

	/** Destroy the job, which must be complete or not started. */
	~FBuildJob();

	// IBuildJob Interface

	inline FStringView GetName() const final { return Name; }
	inline FStringView GetFunction() const final { return FunctionName; }
	inline EBuildPolicy GetPolicy() const final { return BuildPolicy; }
	inline EPriority GetPriority() const final { return Priority; }

	inline ICache& GetCache() const final { return Cache; }
	inline IBuild& GetBuild() const final { return BuildSystem; }

	void Start(IBuildScheduler& Scheduler, EBuildPolicy Policy, EPriority Priority, FOnBuildJobComplete&& OnComplete) final;

	void Schedule() final;

	// IRequest Interface

	void SetPriority(EPriority Priority) final;
	void Cancel() final;
	void Wait() final;
	bool Poll() final;
	void AddRef() const final;
	void Release() const final;

private:
	void BeginJob();
	void EndJob();

	void CreateContext();

	void EnterCacheQuery();
	void EnterCacheStore();
	void EnterResolveKey();
	void EnterResolveInputMeta();
	void EnterResolveInputData();
	void EnterExecuteRemote();
	void EnterExecuteLocal();

	void BeginCacheQuery();
	void BeginCacheStore();
	void BeginResolveKey();
	void BeginResolveInputMeta();
	void BeginResolveInputData();
	void BeginExecuteRemote();
	void BeginExecuteLocal();

	void EndCacheQuery(FCacheGetCompleteParams&& Params);
	void EndCacheStore(FCachePutCompleteParams&& Params);
	void EndResolveKey(FBuildKeyResolvedParams&& Params);
	void EndResolveInputMeta(FBuildInputMetaResolvedParams&& Params);
	void EndResolveInputData(FBuildInputDataResolvedParams&& Params);
	void EndExecuteRemote(FBuildWorkerActionCompleteParams&& Params);
	void EndExecuteLocal();

	void SkipExecuteRemote() final;

	void CreateAction(TConstArrayView<FBuildInputMetaByKey> InputMeta);

	void SetDefinition(FBuildDefinition&& Definition);
	void SetAction(FBuildAction&& Action);
	void SetInputs(FBuildInputs&& Inputs);
	void SetOutput(const FBuildOutput& Output) final;
	void SetOutputNoCheck(FBuildOutput&& Output);

	/** Terminate the job and send the error to the output complete callback. */
	void CompleteWithError(FStringView Error);

	/** Start execution of an async operation that is managed by a request. */
	void ExecuteAsync(TFunctionRef<FRequest ()> Operation);

	/** Advance to the new state, dispatching to the scheduler and invoking callbacks as appropriate. */
	void AdvanceToState(EBuildJobState NewState);

	/** Execute a transition from the old state to the new state. */
	void ExecuteTransition(EBuildJobState OldState, EBuildJobState NewState);

	/** Execute the new state. */
	void ExecuteState(EBuildJobState NewState);

	/** Exports the action and inputs for this build to disk. */
	void ExportBuild() const;

	/** Determine whether the action and inputs for this build should be exported to disk. */
	bool ShouldExportBuild() const;

	/** Parse the types to export from the command line. Sorted by FNameFastLess. */
	static TArray<FName> ParseExportBuildTypes(bool& bOutExportAll);

private:
	FString Name;
	FString FunctionName{TEXT("Unknown")};

	/** Active state for the job. Always moves through states in order. */
	EBuildJobState State{EBuildJobState::NotStarted};
	/** Next state for the job. Used to handle re-entrant calls to AdvanceToState. */
	EBuildJobState NextState{EBuildJobState::NotStarted};

	EBuildPolicy BuildPolicy{};
	EPriority Priority{};

	/** True if the build was canceled before it was complete. */
	bool bIsCanceled{};
	/** True if the build was retrieved from or stored to the cache. */
	bool bIsCached{};
	/** True if AdvanceToState is executing. */
	bool bInAdvanceToState{};
	/** True if remote execution was attempted, whether or not it failed. */
	bool bTriedRemoteExecution{};
	/** True if the build action and inputs are being exported. */
	bool bIsExportingBuild{};

	mutable std::atomic<uint32> ReferenceCount{0};

	/** Available in [ResolveKey, Complete] for jobs created from a key or definition. */
	FBuildKey DefinitionKey;
	/** Available in [ResolveAction, ExecuteLocal) for jobs created from a key or definition. */
	FOptionalBuildDefinition Definition;
	/** Available in [CacheQuery, CacheStore). */
	FOptionalBuildAction Action;
	/** Available in [ExecuteLocal, CacheStore) for jobs with non-constant inputs. */
	FOptionalBuildInputs Inputs;
	/** Available in [CacheStore, Complete). */
	FOptionalBuildOutput Output;

	/** Available in [NotStarted, CacheStore). */
	FBuildOutputBuilder OutputBuilder;

	/** Available in [CacheQuery, CacheStoreWait). Context used by IBuildFunction. */
	TRefCountPtr<FBuildJobContext> Context;

	FBuildSchedulerParams SchedulerParams;

	/** Lock to synchronize writes to State, NextState, bIsCanceled, Event, WaitRequest, Scheduler. */
	FRWLock Lock;
	/** Event used by and created by Wait(). Null until Wait() has been called on an incomplete job. */
	FEvent* Event{};
	/** Request that the job is waiting on in its wait states. Null outside of states that use it. */
	FRequest WaitRequest;
	/** Scheduler that the job is scheduled on. Null before Schedule() and after completion. */
	IBuildScheduler* Scheduler{};
	/** Resolver for definitions and inputs. May be null for a job with nothing to resolve. */
	IBuildInputResolver* InputResolver{};
	/** Worker to use for remote execution. Available in [ExecuteRemote, ExecuteRemoteRetryWait]. */
	FBuildWorker* Worker{};
	/** Worker executor to use for remote execution. Available in [ExecuteRemote, ExecuteRemoteRetryWait]. */
	IBuildWorkerExecutor* WorkerExecutor{};
	/** Invoked exactly once when the output is complete or when the job fails. */
	FOnBuildJobComplete OnComplete;

	/** Keys for missing inputs. */
	TArray<FString> MissingInputs;

	ICache& Cache;
	IBuild& BuildSystem;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* LexToString(EBuildJobState State)
{
	switch (State)
	{
	case EBuildJobState::NotStarted:                 return TEXT("NotStarted");
	case EBuildJobState::ResolveKey:                 return TEXT("ResolveKey");
	case EBuildJobState::ResolveKeyWait:             return TEXT("ResolveKeyWait");
	case EBuildJobState::ResolveInputMeta:           return TEXT("ResolveInputMeta");
	case EBuildJobState::ResolveInputMetaWait:       return TEXT("ResolveInputMetaWait");
	case EBuildJobState::CacheQuery:                 return TEXT("CacheQuery");
	case EBuildJobState::CacheQueryWait:             return TEXT("CacheQueryWait");
	case EBuildJobState::ExecuteRemote:              return TEXT("ExecuteRemote");
	case EBuildJobState::ExecuteRemoteWait:          return TEXT("ExecuteRemoteWait");
	case EBuildJobState::ResolveRemoteInputData:     return TEXT("ResolveRemoteInputData");
	case EBuildJobState::ResolveRemoteInputDataWait: return TEXT("ResolveRemoteInputDataWait");
	case EBuildJobState::ExecuteRemoteRetry:         return TEXT("ExecuteRemoteRetry");
	case EBuildJobState::ExecuteRemoteRetryWait:     return TEXT("ExecuteRemoteRetryWait");
	case EBuildJobState::ResolveInputData:           return TEXT("ResolveInputData");
	case EBuildJobState::ResolveInputDataWait:       return TEXT("ResolveInputDataWait");
	case EBuildJobState::ExecuteLocal:               return TEXT("ExecuteLocal");
	case EBuildJobState::ExecuteLocalWait:           return TEXT("ExecuteLocalWait");
	case EBuildJobState::CacheStore:                 return TEXT("CacheStore");
	case EBuildJobState::CacheStoreWait:             return TEXT("CacheStoreWait");
	case EBuildJobState::Complete:                   return TEXT("Complete");
	default: checkNoEntry();                         return TEXT("Unknown");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildJob::FBuildJob(
	ICache& InCache,
	IBuild& InBuildSystem,
	IBuildInputResolver* InInputResolver,
	const FBuildKey& InKey)
	: Name(WriteToString<64>(TEXT("Resolve: "_SV), InKey))
	, DefinitionKey(InKey)
	, OutputBuilder(InBuildSystem.CreateOutput(Name, FunctionName))
	, InputResolver(InInputResolver)
	, Cache(InCache)
	, BuildSystem(InBuildSystem)
{
}

FBuildJob::FBuildJob(
	ICache& InCache,
	IBuild& InBuildSystem,
	IBuildInputResolver* InInputResolver,
	const FBuildDefinition& InDefinition)
	: Name(InDefinition.GetName())
	, FunctionName(InDefinition.GetFunction())
	, DefinitionKey(InDefinition.GetKey())
	, Definition(InDefinition)
	, OutputBuilder(InBuildSystem.CreateOutput(Name, FunctionName))
	, InputResolver(InInputResolver)
	, Cache(InCache)
	, BuildSystem(InBuildSystem)
{
}

FBuildJob::FBuildJob(
	ICache& InCache,
	IBuild& InBuildSystem,
	IBuildInputResolver* InInputResolver,
	const FBuildAction& InAction,
	const FOptionalBuildInputs& InInputs)
	: Name(InAction.GetName())
	, FunctionName(InAction.GetFunction())
	, Action(InAction)
	, Inputs(InInputs)
	, OutputBuilder(InBuildSystem.CreateOutput(Name, FunctionName))
	, InputResolver(InInputResolver)
	, Cache(InCache)
	, BuildSystem(InBuildSystem)
{
}

FBuildJob::~FBuildJob()
{
	checkf(State == EBuildJobState::NotStarted || State == EBuildJobState::Complete,
		TEXT("Job in state %s must complete before being destroyed for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	checkf(!OnComplete,
		TEXT("Job in state %s must invoke its completion callback before being destroyed for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	FPlatformProcess::ReturnSynchEventToPool(Event);
}

void FBuildJob::Start(
	IBuildScheduler& InScheduler,
	EBuildPolicy InPolicy,
	EPriority InPriority,
	FOnBuildJobComplete&& InOnComplete)
{
	checkf(State == EBuildJobState::NotStarted,
		TEXT("Job in state %s was previously scheduled for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	Scheduler = &InScheduler;
	BuildPolicy = InPolicy;
	Priority = InPriority;
	OnComplete = MoveTemp(InOnComplete);
	return AdvanceToState(EBuildJobState::ResolveKey);
}

void FBuildJob::Schedule()
{
	switch (State)
	{
	case EBuildJobState::ResolveKey:             return AdvanceToState(EBuildJobState::ResolveKeyWait);
	case EBuildJobState::ResolveInputMeta:       return AdvanceToState(EBuildJobState::ResolveInputMetaWait);
	case EBuildJobState::CacheQuery:             return AdvanceToState(EBuildJobState::CacheQueryWait);
	case EBuildJobState::ExecuteRemote:          return AdvanceToState(EBuildJobState::ExecuteRemoteWait);
	case EBuildJobState::ResolveRemoteInputData: return AdvanceToState(EBuildJobState::ResolveRemoteInputDataWait);
	case EBuildJobState::ExecuteRemoteRetry:     return AdvanceToState(EBuildJobState::ExecuteRemoteRetryWait);
	case EBuildJobState::ResolveInputData:       return AdvanceToState(EBuildJobState::ResolveInputDataWait);
	case EBuildJobState::ExecuteLocal:           return AdvanceToState(EBuildJobState::ExecuteLocalWait);
	case EBuildJobState::CacheStore:             return AdvanceToState(EBuildJobState::CacheStoreWait);
	default:
		checkf(false,
			TEXT("Job in state %s is not valid to be scheduled for build of '%s' by %s."),
			LexToString(State), *Name, *FunctionName);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::SetPriority(EPriority InPriority)
{
	IBuildScheduler* LocalScheduler;
	{
		FWriteScopeLock WriteLock(Lock);
		Priority = InPriority;
		LocalScheduler = Scheduler;
		WaitRequest.SetPriority(InPriority);
	}
	if (LocalScheduler)
	{
		LocalScheduler->UpdateJobPriority(this);
	}
}

void FBuildJob::Cancel()
{
	FRequest LocalRequest;
	IBuildScheduler* LocalScheduler;
	TRefCountPtr<FBuildJobContext> LocalContext;
	if (FReadScopeLock ReadLock(Lock); State == EBuildJobState::Complete)
	{
		return;
	}
	else
	{
		bIsCanceled = true;
		LocalRequest = WaitRequest;
		LocalScheduler = Scheduler;
		if (State == EBuildJobState::ExecuteLocalWait)
		{
			LocalContext = Context;
		}
	}

	// Cancel the job on the scheduler, which invokes Schedule if the job was queued.
	if (LocalScheduler)
	{
		LocalScheduler->CancelJob(this);
	}

	// Cancel the request, which invokes End[State] if the request is not complete.
	if (LocalRequest)
	{
		LocalRequest.Cancel();
	}

	// Cancel the async build, which invokes EndExecuteLocal if the build is not complete.
	if (LocalContext)
	{
		LocalContext->GetFunction().CancelAsyncBuild(*LocalContext);
	}

	// Most jobs will be complete at this point, but it is possible for a job to reach this point
	// because of a race with the scheduler. A job can be removed from every scheduler queue, and
	// missed by CancelJob, but not yet have called back into the job.
	Wait();
}

void FBuildJob::Wait()
{
	FEvent* LocalEvent;

	if (FReadScopeLock ReadLock(Lock); State == EBuildJobState::Complete)
	{
		return;
	}
	else
	{
		LocalEvent = Event;
	}

	SetPriority(EPriority::Blocking);

	if (!LocalEvent)
	{
		if (FWriteScopeLock WriteLock(Lock); State == EBuildJobState::Complete)
		{
			return;
		}
		else if (Event)
		{
			LocalEvent = Event;
		}
		else
		{
			LocalEvent = Event = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset*/ true);
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::Wait);
	LocalEvent->Wait();
}

bool FBuildJob::Poll()
{
	FReadScopeLock ReadLock(Lock);
	return State == EBuildJobState::Complete;
}

void FBuildJob::AddRef() const
{
	ReferenceCount.fetch_add(1, std::memory_order_relaxed);
}

void FBuildJob::Release() const
{
	if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		delete this;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::BeginJob()
{
	AddRef();
	Scheduler->BeginJob(this);
}

void FBuildJob::EndJob()
{
	ON_SCOPE_EXIT { Release(); };

	if (Event)
	{
		Event->Trigger();
	}
	Scheduler->EndJob(this);

	FWriteScopeLock WriteLock(Lock);
	Scheduler = nullptr;
	InputResolver = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::CreateContext()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::CreateContext);

	const IBuildFunction* Function = BuildSystem.GetFunctionRegistry().FindFunction(FunctionName);
	if (BuildSystem.GetVersion() != Action.Get().GetBuildSystemVersion())
	{
		return CompleteWithError(WriteToString<192>(TEXT("Failed because the build system is version "_SV),
			BuildSystem.GetVersion(), TEXT(" when version "_SV),
			Action.Get().GetBuildSystemVersion(), TEXT(" is expected."_SV)));
	}
	else if (!Function)
	{
		return CompleteWithError(WriteToString<128>(TEXT("Failed because the function "_SV), FunctionName,
			TEXT(" was not found."_SV)));
	}
	else if (!Function->GetVersion().IsValid())
	{
		return CompleteWithError(WriteToString<128>(TEXT("Failed because the function "_SV), FunctionName,
			TEXT(" has a version of zero."_SV)));
	}
	else if (Function->GetVersion() != Action.Get().GetFunctionVersion())
	{
		return CompleteWithError(WriteToString<192>(TEXT("Failed because the function "_SV), FunctionName,
			TEXT(" is version "_SV), Function->GetVersion(), TEXT(" when version "_SV),
			Action.Get().GetFunctionVersion(), TEXT(" is expected."_SV)));
	}
	else
	{
		const FCacheKey CacheKey{Cache.CreateBucket(FunctionName), Action.Get().GetKey().Hash};
		Context = new FBuildJobContext(*this, CacheKey, *Function, OutputBuilder, BuildPolicy,
			[this] { EndExecuteLocal(); });
		Function->Configure(*Context);
		BuildPolicy = Context->GetBuildPolicy();
		bIsExportingBuild = ShouldExportBuild();
	}

	// Populate the scheduler params with the information that is available now.
	SchedulerParams.Key = Action.Get().GetKey();
	Action.Get().IterateConstants([this](FStringView Key, FCbObject&& Value)
	{
		const uint64 ValueSize = Value.GetSize();
		SchedulerParams.TotalInputsSize += ValueSize;
		SchedulerParams.ResolvedInputsSize += ValueSize;
	});
	Action.Get().IterateInputs([this](FStringView Key, const FIoHash& RawHash, uint64 RawSize)
	{
		SchedulerParams.TotalInputsSize += RawSize;
	});
	if (Inputs)
	{
		Inputs.Get().IterateInputs([this](FStringView Key, const FCompressedBuffer& Buffer)
		{
			SchedulerParams.ResolvedInputsSize += Buffer.GetRawSize();
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::EnterCacheQuery()
{
	ECachePolicy CachePolicy = Context ? Context->GetCachePolicy() : ECachePolicy::None;
	if (!EnumHasAnyFlags(CachePolicy, ECachePolicy::Query) ||
		EnumHasAnyFlags(BuildPolicy, EBuildPolicy::SkipCacheGet))
	{
		return AdvanceToState(EBuildJobState::ExecuteRemote);
	}
}

void FBuildJob::BeginCacheQuery()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::CacheQuery);
	return ExecuteAsync([this]
	{
		ECachePolicy CachePolicy = Context->GetCachePolicy();
		if (EnumHasAnyFlags(BuildPolicy, EBuildPolicy::SkipData))
		{
			CachePolicy |= ECachePolicy::SkipAttachments;
		}
		return Cache.Get({Context->GetCacheKey()}, Name, CachePolicy, Priority,
			[this](FCacheGetCompleteParams&& Params) { EndCacheQuery(MoveTemp(Params)); });
	});
}

void FBuildJob::EndCacheQuery(FCacheGetCompleteParams&& Params)
{
	if (Params.Status == EStatus::Ok)
	{
		if (FOptionalBuildOutput CacheOutput = BuildSystem.LoadOutput(Name, FunctionName, Params.Record))
		{
			bIsCached = true;
			return SetOutputNoCheck(MoveTemp(CacheOutput).Get());
		}
	}
	return AdvanceToState(EBuildJobState::ExecuteRemote);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::EnterCacheStore()
{
	ECachePolicy CachePolicy = Context ? Context->GetCachePolicy() : ECachePolicy::None;
	if (bIsCached ||
		!EnumHasAnyFlags(CachePolicy, ECachePolicy::Store) ||
		EnumHasAnyFlags(BuildPolicy, EBuildPolicy::SkipCachePut) ||
		Output.Get().HasError())
	{
		return AdvanceToState(EBuildJobState::Complete);
	}
}

void FBuildJob::BeginCacheStore()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::CacheStore);
	return ExecuteAsync([this]
	{
		FCacheRecordBuilder RecordBuilder = Cache.CreateRecord(Context->GetCacheKey());
		Output.Get().Save(RecordBuilder);
		return Cache.Put({RecordBuilder.Build()}, Name, Context->GetCachePolicy(),
			FMath::Min(Priority, EPriority::Highest),
			[this](FCachePutCompleteParams&& Params) { EndCacheStore(MoveTemp(Params)); });
	});
}

void FBuildJob::EndCacheStore(FCachePutCompleteParams&& Params)
{
	bIsCached = Params.Status == EStatus::Ok;
	return AdvanceToState(EBuildJobState::Complete);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::EnterResolveKey()
{
	if (Action)
	{
		return AdvanceToState(EBuildJobState::CacheQuery);
	}
	if (Definition)
	{
		return AdvanceToState(EBuildJobState::ResolveInputMeta);
	}
	if (DefinitionKey != FBuildKey::Empty)
	{
		return CompleteWithError(TEXT("Failed to resolve null key."_SV));
	}
	if (!InputResolver)
	{
		return CompleteWithError(TEXT("Failed to resolve key due to null input resolver."_SV));
	}
}

void FBuildJob::BeginResolveKey()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::ResolveKey);
	return ExecuteAsync([this]
	{
		return InputResolver->ResolveKey(DefinitionKey,
			[this](FBuildKeyResolvedParams&& Params) { EndResolveKey(MoveTemp(Params)); });
	});
}

void FBuildJob::EndResolveKey(FBuildKeyResolvedParams&& Params)
{
	if (Params.Status == EStatus::Ok && Params.Definition)
	{
		return SetDefinition(MoveTemp(Params.Definition).Get());
	}
	else
	{
		return CompleteWithError(WriteToString<128>(TEXT("Failed to resolve key "_SV), Params.Key, TEXT("."_SV)));
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::EnterResolveInputMeta()
{
	if (!Definition.Get().HasInputs())
	{
		return CreateAction({});
	}
	if (!InputResolver)
	{
		return CompleteWithError(TEXT("Failed to resolve input metadata due to null input resolver."_SV));
	}
}

void FBuildJob::BeginResolveInputMeta()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::ResolveInputMeta);
	return ExecuteAsync([this]
	{
		return InputResolver->ResolveInputMeta(Definition.Get(), Priority,
			[this](FBuildInputMetaResolvedParams&& Params) { EndResolveInputMeta(MoveTemp(Params)); });
	});
}

void FBuildJob::EndResolveInputMeta(FBuildInputMetaResolvedParams&& Params)
{
	if (Params.Status == EStatus::Ok)
	{
		return CreateAction(Params.Inputs);
	}
	else
	{
		return CompleteWithError(TEXT("Failed to resolve input metadata."_SV));
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::EnterResolveInputData()
{
	SchedulerParams.MissingLocalInputsSize = 0;
	SchedulerParams.MissingRemoteInputsSize = 0;
	if (State == EBuildJobState::ResolveInputData)
	{
		MissingInputs.Reset();
		Action.Get().IterateInputs([this](FStringView Key, const FIoHash& RawHash, uint64 RawSize)
		{
			if (!Inputs || Inputs.Get().FindInput(Key).IsNull())
			{
				MissingInputs.Emplace(Key);
				SchedulerParams.MissingLocalInputsSize += RawSize;
			}
		});
		if (MissingInputs.IsEmpty())
		{
			return AdvanceToState(EBuildJobState::ExecuteLocal);
		}
	}
	else
	{
		Action.Get().IterateInputs([this](FStringView Key, const FIoHash& RawHash, uint64 RawSize)
		{
			if (MissingInputs.FindByKey(Key))
			{
				SchedulerParams.MissingRemoteInputsSize += RawSize;
			}
		});
	}

	checkf(!MissingInputs.IsEmpty(),
		TEXT("Job is not expected to be in state %s without missing inputs for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);

	if (!InputResolver)
	{
		return CompleteWithError(TEXT("Failed to resolve input data due to null input resolver."_SV));
	}
}

void FBuildJob::BeginResolveInputData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::ResolveInputData);
	return ExecuteAsync([this]
	{
		if (Definition)
		{
			return InputResolver->ResolveInputData(Definition.Get(), Priority,
				[this](FBuildInputDataResolvedParams&& Params) { EndResolveInputData(MoveTemp(Params)); },
				[this](FStringView Key) { return !!Algo::Find(MissingInputs, Key); });
		}
		else
		{
			return InputResolver->ResolveInputData(Action.Get(), Priority,
				[this](FBuildInputDataResolvedParams&& Params) { EndResolveInputData(MoveTemp(Params)); },
				[this](FStringView Key) { return !!Algo::Find(MissingInputs, Key); });
		}
	});
}

void FBuildJob::EndResolveInputData(FBuildInputDataResolvedParams&& Params)
{
	if (Params.Status == EStatus::Ok)
	{
		FBuildInputsBuilder Builder = BuildSystem.CreateInputs(Name);
		for (const FBuildInputDataByKey& Input : Params.Inputs)
		{
			Builder.AddInput(Input.Key, Input.Data);
			SchedulerParams.ResolvedInputsSize += Input.Data.GetRawSize();
		}
		if (Inputs)
		{
			Inputs.Get().IterateInputs([&Builder](FStringView Key, const FCompressedBuffer& Buffer)
			{
				Builder.AddInput(Key, Buffer);
			});
		}
		return SetInputs(Builder.Build());
	}
	else
	{
		return CompleteWithError(TEXT("Failed to resolve input data."_SV));
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::EnterExecuteRemote()
{
	if (EnumHasAnyFlags(BuildPolicy, EBuildPolicy::SkipBuild))
	{
		return CompleteWithError(TEXT("Failed because build policy skipped building."_SV));
	}
	if (!EnumHasAnyFlags(BuildPolicy, EBuildPolicy::Remote) || bIsExportingBuild)
	{
		return AdvanceToState(EBuildJobState::ResolveInputData);
	}

	if (!Worker || !WorkerExecutor)
	{
		Worker = BuildSystem.GetWorkerRegistry().FindWorker(
			Action.Get().GetFunction(),
			Action.Get().GetFunctionVersion(),
			Action.Get().GetBuildSystemVersion(),
			WorkerExecutor);
	}

	if (!Worker || !WorkerExecutor)
	{
		return AdvanceToState(EBuildJobState::ResolveInputData);
	}
}

void FBuildJob::BeginExecuteRemote()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::ExecuteRemote);
	checkf(Worker && WorkerExecutor, TEXT("Job requires a worker in state %s for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	bTriedRemoteExecution = true;
	return ExecuteAsync([this]
	{
		return WorkerExecutor->BuildAction(Action.Get(), Inputs, *Worker, BuildPolicy, Priority,
			[this](FBuildWorkerActionCompleteParams&& Params) { EndExecuteRemote(MoveTemp(Params)); });
	});
}

void FBuildJob::EndExecuteRemote(FBuildWorkerActionCompleteParams&& Params)
{
	if (Params.Output)
	{
		return SetOutputNoCheck(MoveTemp(Params.Output).Get());
	}
	else
	{
		switch (State)
		{
		case EBuildJobState::ExecuteRemote:
		case EBuildJobState::ExecuteRemoteWait:
			MissingInputs.Reset(Params.MissingInputs.Num());
			for (const FStringView& Key : Params.MissingInputs)
			{
				MissingInputs.Emplace(Key);
			}
			if (!MissingInputs.IsEmpty())
			{
				MissingInputs.Sort();
				return AdvanceToState(EBuildJobState::ResolveRemoteInputData);
			}
			[[fallthrough]];
		case EBuildJobState::ExecuteRemoteRetry:
		case EBuildJobState::ExecuteRemoteRetryWait:
		default:
			return AdvanceToState(EBuildJobState::ResolveInputData);
		}
	}
}

void FBuildJob::SkipExecuteRemote()
{
	checkf(State == EBuildJobState::ResolveRemoteInputData ||
		State == EBuildJobState::ExecuteRemote || State == EBuildJobState::ExecuteRemoteRetry,
		TEXT("Job is not expecting SkipExecuteRemote to be called in state %s for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	return AdvanceToState(EBuildJobState::ResolveInputData);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::EnterExecuteLocal()
{
	if (bIsExportingBuild)
	{
		ExportBuild();
	}
	if (!EnumHasAnyFlags(BuildPolicy, EBuildPolicy::Local))
	{
		if (bTriedRemoteExecution)
		{
			return CompleteWithError(TEXT("Failed because build policy does not allow local execution, ")
				TEXT("and remote execution failed to build."_SV));
		}
		else
		{
			return CompleteWithError(TEXT("Failed because build policy does not allow local execution, ")
				TEXT("and remote execution was not available."_SV));
		}
	}
	Action.Get().IterateConstants([this](FStringView Key, FCbObject&& Value)
	{
		Context->AddConstant(Key, MoveTemp(Value));
	});
	Action.Get().IterateInputs([this](FStringView Key, const FIoHash& RawHash, uint64 RawSize)
	{
		const FCompressedBuffer& Buffer = Inputs.Get().FindInput(Key);
		if (Buffer.GetRawHash() == RawHash && Buffer.GetRawSize() == RawSize)
		{
			Context->AddInput(Key, Buffer);
		}
		else
		{
			CompleteWithError(WriteToString<256>(TEXT("Failed because input '"_SV), Key, TEXT("' with hash "_SV),
				RawHash, TEXT(" ("_SV), RawSize, TEXT(" bytes) was resolved with hash "_SV), Buffer.GetRawHash(),
				TEXT(" ("_SV), Buffer.GetRawSize(), TEXT(" bytes)."_SV)));
		}
	});
	Action.Reset();
	Inputs.Reset();
	SchedulerParams.ResolvedInputsSize = 0;
}

void FBuildJob::BeginExecuteLocal()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FBuildJob::ExecuteLocal);
	return ExecuteAsync([this]
	{
		Context->GetFunction().Build(*Context);
		if (!Context->IsAsyncBuild())
		{
			EndExecuteLocal();
		}
		return FRequest();
	});
}

void FBuildJob::EndExecuteLocal()
{
	return SetOutputNoCheck(OutputBuilder.Build());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::CreateAction(TConstArrayView<FBuildInputMetaByKey> InputMeta)
{
	const FBuildDefinition& LocalDefinition = Definition.Get();
	FBuildActionBuilder Builder = BuildSystem.CreateAction(Name, FunctionName);
	LocalDefinition.IterateConstants([this, &Builder](FStringView Key, FCbObject&& Value)
	{
		Builder.AddConstant(Key, Value);
	});
	for (const FBuildInputMetaByKey& Input : InputMeta)
	{
		Builder.AddInput(Input.Key, Input.RawHash, Input.RawSize);
	}
	return SetAction(Builder.Build());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::SetDefinition(FBuildDefinition&& InDefinition)
{
	Name = InDefinition.GetName();
	FunctionName = InDefinition.GetFunction();
	checkf(Definition.IsNull(), TEXT("Job already has a definition for build of '%s' by %s."), *Name, *FunctionName);
	checkf(State == EBuildJobState::ResolveKey || State == EBuildJobState::ResolveKeyWait,
		TEXT("Job is not expecting a definition in state %s for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	OutputBuilder = BuildSystem.CreateOutput(Name, FunctionName);
	Definition = MoveTemp(InDefinition);
	return AdvanceToState(EBuildJobState::ResolveInputMeta);
}

void FBuildJob::SetAction(FBuildAction&& InAction)
{
	checkf(Action.IsNull(), TEXT("Job already has an action for build of '%s' by %s."), *Name, *FunctionName);
	checkf(State == EBuildJobState::ResolveKey ||
		State == EBuildJobState::ResolveInputMeta || State == EBuildJobState::ResolveInputMetaWait,
		TEXT("Job is not expecting an action in state %s for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	Action = MoveTemp(InAction);
	return AdvanceToState(EBuildJobState::CacheQuery);
}

void FBuildJob::SetInputs(FBuildInputs&& InInputs)
{
	EBuildJobState ExecuteState;
	switch (State)
	{
	case EBuildJobState::ResolveRemoteInputData:
	case EBuildJobState::ResolveRemoteInputDataWait:
		ExecuteState = EBuildJobState::ExecuteRemoteRetry;
		break;
	case EBuildJobState::ResolveInputData:
	case EBuildJobState::ResolveInputDataWait:
		ExecuteState = EBuildJobState::ExecuteLocal;
		break;
	default:
		checkf(false, TEXT("Job is not expecting inputs in state %s for build of '%s' by %s."),
			LexToString(State), *Name, *FunctionName);
		return;
	}
	Inputs = MoveTemp(InInputs);
	SchedulerParams.MissingLocalInputsSize = 0;
	SchedulerParams.MissingRemoteInputsSize = 0;
	return AdvanceToState(ExecuteState);
}

void FBuildJob::SetOutput(const FBuildOutput& InOutput)
{
	checkf(State == EBuildJobState::ResolveKey || State == EBuildJobState::ResolveInputMeta ||
		State == EBuildJobState::CacheQuery || State == EBuildJobState::ResolveInputData ||
		State == EBuildJobState::ExecuteRemote || State == EBuildJobState::ExecuteRemoteRetry ||
		State == EBuildJobState::ExecuteLocal,
		TEXT("Job is not expecting an output in state %s for build of '%s' by %s."),
		LexToString(State), *Name, *FunctionName);
	return SetOutputNoCheck(FBuildOutput(InOutput));
}

void FBuildJob::SetOutputNoCheck(FBuildOutput&& InOutput)
{
	checkf(Output.IsNull(), TEXT("Job already has an output for build of '%s' by %s."), *Name, *FunctionName);
	Output = MoveTemp(InOutput);

	if (!bIsCanceled)
	{
		Scheduler->SetJobOutput(this, SchedulerParams, Output.Get());
	}

	if (OnComplete)
	{
		const EStatus Status = bIsCanceled ? EStatus::Canceled : Output.Get().HasError() ? EStatus::Error : EStatus::Ok;
		OnComplete({*this, FBuildOutput(Output.Get()), Status});
		OnComplete = nullptr;
	}

	return AdvanceToState(EBuildJobState::CacheStore);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildJob::CompleteWithError(FStringView Error)
{
	if (FWriteScopeLock WriteLock(Lock); Output || NextState == EBuildJobState::Complete)
	{
		return;
	}
	OutputBuilder.AddError(TEXT("LogDerivedDataBuild"_SV), Error);
	return SetOutputNoCheck(OutputBuilder.Build());
}

void FBuildJob::ExecuteAsync(TFunctionRef<FRequest ()> Operation)
{
	// An operation may complete and advance to another state before its request has been stored.
	// Hold a reference to allow access to this after starting the operation.
	const TRequest Self(this);
	const EBuildJobState LocalState = State;
	if (FRequest LocalRequest = Operation())
	{
		if (FWriteScopeLock WriteLock(Lock); LocalState == State && !bIsCanceled)
		{
			// The state flow through the job requires any previous WaitRequest to be complete by
			// the time that ExecuteAsync is called because the completion of the async operation
			// will request the state transition that leads to another new async operation. It is
			// not possible to poll the previous request to validate that it is complete, because
			// execution likely reached this point from the completion callback, and a request is
			// not complete until its completion callback has returned.
			WaitRequest = MoveTemp(LocalRequest);
		}
		// Cancel the request if it was not moved above due to cancellation or change of state.
		LocalRequest.Cancel();
	}
}

void FBuildJob::AdvanceToState(EBuildJobState NewState)
{
	EBuildJobState OldState = EBuildJobState::Complete;

	if (FWriteScopeLock WriteLock(Lock); NextState < NewState)
	{
		// TODO: Improve CompleteWithError to avoid unexpected state transitions.
		//checkf(NextState < NewState,
		//	TEXT("Job in state %s is requesting an invalid transition from %s to %s for build of '%s' by %s."),
		//	LexToString(State), LexToString(NextState), LexToString(NewState), *Name, *FunctionName);

		if (bIsCanceled)
		{
			NewState = EBuildJobState::Complete;
		}

		NextState = NewState;

		if (bInAdvanceToState)
		{
			return;
		}
		else
		{
			bInAdvanceToState = true;
			OldState = State;
			State = NewState;
		}
	}

	while (OldState < NewState)
	{
		ExecuteTransition(OldState, NewState);

		if (NewState == EBuildJobState::Complete)
		{
			return;
		}

		if (FWriteScopeLock WriteLock(Lock); NewState < NextState)
		{
			OldState = NewState;
			NewState = NextState;
			State = NewState;
		}
		else
		{
			bInAdvanceToState = false;
			break;
		}
	}

	ExecuteState(NewState);
}

void FBuildJob::ExecuteTransition(EBuildJobState OldState, EBuildJobState NewState)
{
	if (OldState == EBuildJobState::NotStarted)
	{
		BeginJob();
	}
	if (OldState < EBuildJobState::CacheQuery && EBuildJobState::CacheQuery <= NewState && NewState <= EBuildJobState::CacheStoreWait)
	{
		CreateContext();
	}
	if (OldState <= EBuildJobState::ExecuteRemoteRetryWait && EBuildJobState::ExecuteRemoteRetryWait < NewState)
	{
		Worker = nullptr;
		WorkerExecutor = nullptr;
	}
	if (OldState <= EBuildJobState::ResolveInputDataWait && EBuildJobState::ResolveInputDataWait < NewState)
	{
		MissingInputs.Empty();
		Definition.Reset();
	}
	if (OldState <= EBuildJobState::ExecuteLocalWait && EBuildJobState::ExecuteLocalWait < NewState)
	{
		Action.Reset();
		Inputs.Reset();
		if (Context)
		{
			Context->ResetInputs();
		}
		SchedulerParams.ResolvedInputsSize = 0;
	}
	if (OldState <= EBuildJobState::ExecuteLocalWait && EBuildJobState::ExecuteLocalWait < NewState && !Output)
	{
		SetOutputNoCheck(OutputBuilder.Build());
	}
	if (NewState == EBuildJobState::Complete)
	{
		Output.Reset();
		Context = nullptr;
	}

	switch (NewState)
	{
	case EBuildJobState::ResolveKey:                 return EnterResolveKey();
	case EBuildJobState::ResolveKeyWait:             return BeginResolveKey();
	case EBuildJobState::ResolveInputMeta:           return EnterResolveInputMeta();
	case EBuildJobState::ResolveInputMetaWait:       return BeginResolveInputMeta();
	case EBuildJobState::CacheQuery:                 return EnterCacheQuery();
	case EBuildJobState::CacheQueryWait:             return BeginCacheQuery();
	case EBuildJobState::ExecuteRemote:              return EnterExecuteRemote();
	case EBuildJobState::ExecuteRemoteWait:          return BeginExecuteRemote();
	case EBuildJobState::ResolveRemoteInputData:     return EnterResolveInputData();
	case EBuildJobState::ResolveRemoteInputDataWait: return BeginResolveInputData();
	case EBuildJobState::ExecuteRemoteRetry:         return EnterExecuteRemote();
	case EBuildJobState::ExecuteRemoteRetryWait:     return BeginExecuteRemote();
	case EBuildJobState::ResolveInputData:           return EnterResolveInputData();
	case EBuildJobState::ResolveInputDataWait:       return BeginResolveInputData();
	case EBuildJobState::ExecuteLocal:               return EnterExecuteLocal();
	case EBuildJobState::ExecuteLocalWait:           return BeginExecuteLocal();
	case EBuildJobState::CacheStore:                 return EnterCacheStore();
	case EBuildJobState::CacheStoreWait:             return BeginCacheStore();
	case EBuildJobState::Complete:                   return EndJob();
	}
}

void FBuildJob::ExecuteState(EBuildJobState NewState)
{
	switch (NewState)
	{
	case EBuildJobState::ResolveKey:                 return Scheduler->DispatchResolveKey(this);
	case EBuildJobState::ResolveInputMeta:           return Scheduler->DispatchResolveInputMeta(this);
	case EBuildJobState::CacheQuery:                 return Scheduler->DispatchCacheQuery(this, SchedulerParams);
	case EBuildJobState::ExecuteRemote:              return Scheduler->DispatchExecuteRemote(this, SchedulerParams);
	case EBuildJobState::ResolveRemoteInputData:     return Scheduler->DispatchResolveInputData(this, SchedulerParams);
	case EBuildJobState::ExecuteRemoteRetry:         return Scheduler->DispatchExecuteRemote(this, SchedulerParams);
	case EBuildJobState::ResolveInputData:           return Scheduler->DispatchResolveInputData(this, SchedulerParams);
	case EBuildJobState::ExecuteLocal:               return Scheduler->DispatchExecuteLocal(this, SchedulerParams);
	case EBuildJobState::CacheStore:                 return Scheduler->DispatchCacheStore(this, SchedulerParams);
	}
}

void FBuildJob::ExportBuild() const
{
	// Export to <SavedDir>/DerivedDataBuildExport/<Bucket>[/<Function>]/<Action>
	TStringBuilder<256> ExportPath;
	const FCacheKey& Key = Context->GetCacheKey();
	FPathViews::Append(ExportPath, FPaths::ProjectSavedDir(), TEXT("DerivedDataBuildExport"), Key.Bucket);
	if (FunctionName != Key.Bucket.ToString<TCHAR>())
	{
		FPathViews::Append(ExportPath, FunctionName);
	}
	FPathViews::Append(ExportPath, Key.Hash);
	int32 ExportRootLen = ExportPath.Len();

	TAnsiStringBuilder<512> Meta;
	Meta << "Name: " << FTCHARToUTF8(Name) << LINE_TERMINATOR_ANSI;
	Meta << "Cache: " << Key << LINE_TERMINATOR_ANSI;
	Meta << "Function: " << FTCHARToUTF8(FunctionName) << LINE_TERMINATOR_ANSI;
	Meta << "FunctionVersion: " << Action.Get().GetFunctionVersion() << LINE_TERMINATOR_ANSI;
	Meta << "BuildSystemVersion: " << Action.Get().GetBuildSystemVersion() << LINE_TERMINATOR_ANSI;
	if (Action.Get().HasConstants())
	{
		Meta << "Constants:" << LINE_TERMINATOR_ANSI;
		Action.Get().IterateConstants([&Meta](FStringView Key, FCbObject&& Value)
		{
			Meta << "  " << FTCHARToUTF8(Key) << ":" LINE_TERMINATOR_ANSI;
			Meta << "    RawHash: " << FIoHash(Value.GetHash()) << LINE_TERMINATOR_ANSI;
			Meta << "    RawSize: " << Value.GetSize() << LINE_TERMINATOR_ANSI;
		});
	}
	if (Action.Get().HasInputs())
	{
		Meta << "Inputs:" << LINE_TERMINATOR_ANSI;
		Action.Get().IterateInputs([&Meta](FStringView Key, const FIoHash& RawHash, uint64 RawSize)
		{
			Meta << "  " << FTCHARToUTF8(Key) << ":" LINE_TERMINATOR_ANSI;
			Meta << "    RawHash: " << RawHash << LINE_TERMINATOR_ANSI;
			Meta << "    RawSize: " << RawSize << LINE_TERMINATOR_ANSI;
		});
	}

	FPathViews::Append(ExportPath, TEXT("Meta.yaml"));
	if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileWriter(*ExportPath)})
	{
		Ar->Serialize(Meta.GetData(), Meta.Len());
	}
	ExportPath.RemoveSuffix(ExportPath.Len() - ExportRootLen);

	FPathViews::Append(ExportPath, TEXT("Build.action"));
	if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileWriter(*ExportPath)})
	{
		FCbWriter Writer;
		Action.Get().Save(Writer);
		Writer.Save(*Ar);
	}
	ExportPath.RemoveSuffix(ExportPath.Len() - ExportRootLen);

	if (Inputs)
	{
		Meta << "Inputs:" << LINE_TERMINATOR_ANSI;
		FPathViews::Append(ExportPath, TEXT("Inputs"));
		ExportRootLen = ExportPath.Len();
		Inputs.Get().IterateInputs([&ExportPath, ExportRootLen](FStringView Key, const FCompressedBuffer& Buffer)
		{
			FPathViews::Append(ExportPath, FIoHash(Buffer.GetRawHash()));
			if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileWriter(*ExportPath)})
			{
				*Ar << const_cast<FCompressedBuffer&>(Buffer);
			}
			ExportPath.RemoveSuffix(ExportPath.Len() - ExportRootLen);
		});
	}
}

bool FBuildJob::ShouldExportBuild() const
{
	static bool bExportAll;
	static TArray<FName> ExportTypes = ParseExportBuildTypes(bExportAll);
	return bExportAll
		|| Algo::BinarySearch(ExportTypes, FName(FunctionName), FNameFastLess()) != INDEX_NONE
		|| Algo::BinarySearch(ExportTypes, FName(Context->GetCacheKey().Bucket.ToString<ANSICHAR>()), FNameFastLess()) != INDEX_NONE;
}

TArray<FName> FBuildJob::ParseExportBuildTypes(bool& bOutExportAll)
{
	TArray<FName> ExportTypes;
	if (FString ExportTypesArg; FParse::Value(FCommandLine::Get(), TEXT("-ExportBuilds="), ExportTypesArg))
	{
		String::ParseTokens(ExportTypesArg, TEXT('+'), [&ExportTypes](FStringView Type) { ExportTypes.Emplace(Type); });
		ExportTypes.Sort(FNameFastLess());
		bOutExportAll = false;
	}
	else
	{
		bOutExportAll = FParse::Param(FCommandLine::Get(), TEXT("ExportBuilds"));
	}
	return ExportTypes;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TRequest<IBuildJob> CreateBuildJob(ICache& Cache, IBuild& BuildSystem, IBuildInputResolver* InputResolver, const FBuildKey& Key)
{
	return TRequest(new FBuildJob(Cache, BuildSystem, InputResolver, Key));
}

TRequest<IBuildJob> CreateBuildJob(ICache& Cache, IBuild& BuildSystem, IBuildInputResolver* InputResolver, const FBuildDefinition& Definition)
{
	return TRequest(new FBuildJob(Cache, BuildSystem, InputResolver, Definition));
}

TRequest<IBuildJob> CreateBuildJob(ICache& Cache, IBuild& BuildSystem, IBuildInputResolver* InputResolver, const FBuildAction& Action, const FOptionalBuildInputs& Inputs)
{
	return TRequest(new FBuildJob(Cache, BuildSystem, InputResolver, Action, Inputs));
}

} // UE::DerivedData::Private
