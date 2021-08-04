// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildScheduler.h"

#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "DerivedDataBuildFunctionRegistry.h"
#include "Misc/Guid.h"
#include "Tasks/Task.h"

namespace UE::DerivedData::Private
{

class FBuildSchedulerRequest : public FRequestBase
{
public:
	FBuildSchedulerRequest(IBuildJob* InJob, IRequestOwner& InOwner, const TCHAR* DebugName)
		: Job(InJob)
		, Owner(InOwner)
	{
		Tasks::FTaskEvent TaskEvent(TEXT("FBuildSchedulerRequest"));
		Task = Tasks::Launch(DebugName, [this] { Schedule(); }, TaskEvent, Tasks::ETaskPriority::BackgroundNormal);
		Owner.Begin(this);
		TaskEvent.Trigger();
	}

	void Schedule()
	{
		Owner.End(this, [this] { Job->Schedule(); });
	}

	void SetPriority(EPriority Priority) final
	{
	}

	void Cancel() final
	{
		Task.Wait();
	}

	void Wait() final
	{
		Task.Wait();
	}

private:
	IBuildJob* Job;
	IRequestOwner& Owner;
	Tasks::FTask Task;
};

class FBuildScheduler final : public IBuildScheduler
{
public:
	void DispatchCacheQuery(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params) final;
	void DispatchCacheStore(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params) final;
	void DispatchResolveKey(IBuildJob* Job, IRequestOwner& Owner) final;
	void DispatchResolveInputMeta(IBuildJob* Job, IRequestOwner& Owner) final;
	void DispatchResolveInputData(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params) final;
	void DispatchExecuteRemote(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params) final;
	void DispatchExecuteLocal(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params) final;

private:
	void Dispatch(IBuildJob* Job, IRequestOwner& Owner, const TCHAR* DebugName);
};

void FBuildScheduler::DispatchCacheQuery(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params)
{
	Job->Schedule();
}

void FBuildScheduler::DispatchCacheStore(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params)
{
	Job->Schedule();
}

void FBuildScheduler::DispatchResolveKey(IBuildJob* Job, IRequestOwner& Owner)
{
	Dispatch(Job, Owner, TEXT("FBuildScheduler::DispatchResolveKey"));
}

void FBuildScheduler::DispatchResolveInputMeta(IBuildJob* Job, IRequestOwner& Owner)
{
	Dispatch(Job, Owner, TEXT("FBuildScheduler::DispatchResolveInputMeta"));
}

void FBuildScheduler::DispatchResolveInputData(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params)
{
	Dispatch(Job, Owner, TEXT("FBuildScheduler::DispatchResolveInputData"));
}

void FBuildScheduler::DispatchExecuteRemote(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params)
{
	Dispatch(Job, Owner, TEXT("FBuildScheduler::DispatchExecuteRemote"));
}

void FBuildScheduler::DispatchExecuteLocal(IBuildJob* Job, IRequestOwner& Owner, const FBuildSchedulerParams& Params)
{
	Dispatch(Job, Owner, TEXT("FBuildScheduler::DispatchExecuteLocal"));
}

void FBuildScheduler::Dispatch(IBuildJob* Job, IRequestOwner& Owner, const TCHAR* DebugName)
{
	if (Owner.GetPriority() == EPriority::Blocking)
	{
		Job->Schedule();
	}
	else
	{
		new FBuildSchedulerRequest(Job, Owner, DebugName);
	}
}

IBuildScheduler* CreateBuildScheduler()
{
	return new FBuildScheduler();
}

} // UE::DerivedData::Private
