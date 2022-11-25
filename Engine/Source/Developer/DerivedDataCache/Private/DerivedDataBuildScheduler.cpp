// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildScheduler.h"
#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "DerivedDataBuildFunctionRegistry.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataThreadPoolTask.h"
#include "Experimental/DerivedDataBuildSchedulerThreadPoolProvider.h"
#include "Experimental/Misc/ExecutionResource.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"

namespace UE::DerivedData::Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildSchedulerMemoryQueue
{
public:
	FBuildSchedulerMemoryQueue();
	~FBuildSchedulerMemoryQueue();

	void WaitForMemory(
		const FUtf8SharedString& TypeName,
		uint64 MemoryEstimate,
		IRequestOwner& Owner,
		TUniqueFunction<void ()>&& OnComplete);

private:
	void OnModularFeatureRegistered(const FName& Type, IModularFeature* ModularFeature);
	void OnModularFeatureUnregistered(const FName& Type, IModularFeature* ModularFeature);

	void AddProviderNoLock(IBuildSchedulerThreadPoolProvider* Provider);
	void RemoveProvider(IBuildSchedulerThreadPoolProvider* Provider);

	FQueuedThreadPool* FindThreadPool(const FUtf8SharedString& TypeName) const;

private:
	mutable FRWLock Lock;
	TMap<FUtf8SharedString, IBuildSchedulerThreadPoolProvider*> Providers;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildSchedulerMemoryQueue::FBuildSchedulerMemoryQueue()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	for (IBuildSchedulerThreadPoolProvider* Provider : ModularFeatures.GetModularFeatureImplementations<IBuildSchedulerThreadPoolProvider>(IBuildSchedulerThreadPoolProvider::FeatureName))
	{
		AddProviderNoLock(Provider);
	}
	ModularFeatures.OnModularFeatureRegistered().AddRaw(this, &FBuildSchedulerMemoryQueue::OnModularFeatureRegistered);
	ModularFeatures.OnModularFeatureUnregistered().AddRaw(this, &FBuildSchedulerMemoryQueue::OnModularFeatureUnregistered);
}

FBuildSchedulerMemoryQueue::~FBuildSchedulerMemoryQueue()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	ModularFeatures.OnModularFeatureUnregistered().RemoveAll(this);
	ModularFeatures.OnModularFeatureRegistered().RemoveAll(this);
}

void FBuildSchedulerMemoryQueue::OnModularFeatureRegistered(const FName& Type, IModularFeature* ModularFeature)
{
	if (Type == IBuildSchedulerThreadPoolProvider::FeatureName)
	{
		FWriteScopeLock WriteLock(Lock);
		AddProviderNoLock(static_cast<IBuildSchedulerThreadPoolProvider*>(ModularFeature));
	}
}

void FBuildSchedulerMemoryQueue::OnModularFeatureUnregistered(const FName& Type, IModularFeature* ModularFeature)
{
	if (Type == IBuildSchedulerThreadPoolProvider::FeatureName)
	{
		RemoveProvider(static_cast<IBuildSchedulerThreadPoolProvider*>(ModularFeature));
	}
}

void FBuildSchedulerMemoryQueue::AddProviderNoLock(IBuildSchedulerThreadPoolProvider* Provider)
{
	
	const FUtf8SharedString& TypeName = Provider->GetTypeName();
	const uint32 TypeNameHash = GetTypeHash(TypeName);
	if (TypeName.IsEmpty())
	{
		UE_LOG(LogDerivedDataBuild, Error,
			TEXT("An empty type name is not allowed in a build scheduler thread pool provider."));
	}
	else if (Providers.FindByHash(TypeNameHash, TypeName))
	{
		UE_LOG(LogDerivedDataBuild, Error,
			TEXT("More than one build scheduler thread pool provider has been registered with the type name %s."),
			*WriteToString<64>(TypeName));
	}
	else
	{
		Providers.EmplaceByHash(TypeNameHash, TypeName, Provider);
	}
}

void FBuildSchedulerMemoryQueue::RemoveProvider(IBuildSchedulerThreadPoolProvider* Provider)
{
	const FUtf8SharedString& TypeName = Provider->GetTypeName();
	const uint32 TypeNameHash = GetTypeHash(TypeName);
	FWriteScopeLock WriteLock(Lock);
	Providers.RemoveByHash(TypeNameHash, TypeName);
}

FQueuedThreadPool* FBuildSchedulerMemoryQueue::FindThreadPool(const FUtf8SharedString& TypeName) const
{
	const uint32 TypeNameHash = GetTypeHash(TypeName);
	FReadScopeLock ReadLock(Lock);
	IBuildSchedulerThreadPoolProvider* const* Provider = Providers.FindByHash(TypeNameHash, TypeName);
	return Provider ? (*Provider)->GetThreadPool() : nullptr;
}

void FBuildSchedulerMemoryQueue::WaitForMemory(
	const FUtf8SharedString& TypeName,
	const uint64 MemoryEstimate,
	IRequestOwner& Owner,
	TUniqueFunction<void ()>&& OnComplete)
{
	if (FQueuedThreadPool* ThreadPool = FindThreadPool(TypeName))
	{
		LaunchTaskInThreadPool(MemoryEstimate, Owner, *ThreadPool, MoveTemp(OnComplete));
	}
	else
	{
		Invoke(OnComplete);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildJobSchedule final : public IBuildJobSchedule
{
public:
	FBuildJobSchedule(IBuildJob& InJob, IRequestOwner& InOwner, FBuildSchedulerMemoryQueue& InMemoryQueue)
		: Job(InJob)
		, Owner(InOwner)
		, MemoryQueue(InMemoryQueue)
	{
	}

	FBuildSchedulerParams& EditParameters() final { return Params; }

	void ScheduleCacheQuery() final       { StepSync(); }
	void ScheduleCacheStore() final       { StepSync(); }

	void ScheduleResolveKey() final       { StepAsync(TEXT("ResolveKey")); }
	void ScheduleResolveInputMeta() final { StepAsync(TEXT("ResolveInputMeta")); }

	void ScheduleResolveInputData() final
	{
		if (Params.MissingRemoteInputsSize)
		{
			StepAsync(TEXT("ResolveInputData"));
		}
		else
		{
			StepAsyncOrQueue(TEXT("ResolveInputData"));
		}
	}

	void ScheduleExecuteRemote() final    { StepAsync(TEXT("ExecuteRemote")); }
	void ScheduleExecuteLocal() final     { StepAsyncOrQueue(TEXT("ExecuteLocal")); }

	void EndJob() final
	{
		ExecutionResources = nullptr;
	}

private:
	void StepSync()
	{
		Job.StepExecution();
		// DO NOT ACCESS THIS AGAIN PAST THIS POINT!
	}

	void StepAsync(const TCHAR* DebugName)
	{
		if (Owner.GetPriority() == EPriority::Blocking)
		{
			StepSync();
		}
		else
		{
			Owner.LaunchTask(DebugName, [this] { Job.StepExecution(); });
		}
		// DO NOT ACCESS THIS AGAIN PAST THIS POINT!
	}

	void StepAsyncOrQueue(const TCHAR* DebugName)
	{
		check(Params.TotalRequiredMemory >= Params.ResolvedInputsSize);
		const uint64 CurrentMemoryEstimate = Params.TotalRequiredMemory - Params.ResolvedInputsSize;

		// Queue for memory only once, the first time it is needed for local execution.
		// This will occur either when resolving input data for local execution or before beginning local execution.
		// NOTE: No attempt is made to reserve memory prior to remote execution, which may become a problem if remote
		//       execution frequently requires input data to be loaded.
		if (QueuedMemoryEstimate || CurrentMemoryEstimate == 0)
		{
			return StepAsync(DebugName);
		}

		QueuedMemoryEstimate = CurrentMemoryEstimate;
		MemoryQueue.WaitForMemory(Params.TypeName, CurrentMemoryEstimate, Owner, [this, DebugName]
		{
			ExecutionResources = FExecutionResourceContext::Get();
			StepAsync(DebugName);
		});
		// DO NOT ACCESS THIS AGAIN PAST THIS POINT!
	}

private:
	IBuildJob& Job;
	IRequestOwner& Owner;
	FBuildSchedulerParams Params;
	FBuildSchedulerMemoryQueue& MemoryQueue;
	TRefCountPtr<IExecutionResource> ExecutionResources;
	uint64 QueuedMemoryEstimate = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildScheduler final : public IBuildScheduler
{
	TUniquePtr<IBuildJobSchedule> BeginJob(IBuildJob& Job, IRequestOwner& Owner) final
	{
		return MakeUnique<FBuildJobSchedule>(Job, Owner, MemoryQueue);
	}

	FBuildSchedulerMemoryQueue MemoryQueue;
};

IBuildScheduler* CreateBuildScheduler()
{
	return new FBuildScheduler();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData::Private
