// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildSession.h"

#include "Containers/UnrealString.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildJob.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPrivate.h"

namespace UE::DerivedData::Private
{

class FBuildSessionInternal final : public IBuildSessionInternal
{
public:
	FBuildSessionInternal(
		FStringView InName,
		ICache& InCache,
		IBuild& InBuildSystem,
		IBuildScheduler& InScheduler,
		IBuildInputResolver* InInputResolver)
		: Name(InName)
		, Cache(InCache)
		, BuildSystem(InBuildSystem)
		, Scheduler(InScheduler)
		, InputResolver(InInputResolver)
	{
	}

	FStringView GetName() const final { return Name; }

	FRequest Build(
		const FBuildDefinition& Definition,
		EBuildPolicy Policy,
		EPriority Priority,
		FOnBuildComplete&& OnComplete) final;

	FRequest BuildAction(
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		EBuildPolicy Policy,
		EPriority Priority,
		FOnBuildActionComplete&& OnComplete) final;

	FRequest BuildPayload(
		const FBuildPayloadKey& Payload,
		EBuildPolicy Policy,
		EPriority Priority,
		FOnBuildPayloadComplete&& OnComplete) final;

	FString Name;
	ICache& Cache;
	IBuild& BuildSystem;
	IBuildScheduler& Scheduler;
	IBuildInputResolver* InputResolver;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FRequest FBuildSessionInternal::Build(
	const FBuildDefinition& Definition,
	EBuildPolicy Policy,
	EPriority Priority,
	FOnBuildComplete&& OnComplete)
{
	TRequest Job(CreateBuildJob(Cache, BuildSystem, InputResolver, Definition));
	FOnBuildJobComplete OnJobComplete;
	if (OnComplete)
	{
		OnJobComplete = [Definition, OnComplete = MoveTemp(OnComplete)](FBuildJobCompleteParams&& Params)
		{
			OnComplete({Definition.GetKey(), MoveTemp(Params.Output), Params.Status});
		};
	}
	Job->Schedule(Scheduler, Policy, Priority, MoveTemp(OnJobComplete));
	return Job;
}

FRequest FBuildSessionInternal::BuildAction(
	const FBuildAction& Action,
	const FOptionalBuildInputs& Inputs,
	EBuildPolicy Policy,
	EPriority Priority,
	FOnBuildActionComplete&& OnComplete)
{
	TRequest Job(CreateBuildJob(Cache, BuildSystem, InputResolver, Action, Inputs));
	FOnBuildJobComplete OnJobComplete;
	if (OnComplete)
	{
		OnJobComplete = [Action, OnComplete = MoveTemp(OnComplete)](FBuildJobCompleteParams&& Params)
		{
			OnComplete({Action.GetKey(), MoveTemp(Params.Output), Params.Status});
		};
	}
	Job->Schedule(Scheduler, Policy, Priority, MoveTemp(OnJobComplete));
	return Job;
}

FRequest FBuildSessionInternal::BuildPayload(
	const FBuildPayloadKey& PayloadKey,
	EBuildPolicy Policy,
	EPriority Priority,
	FOnBuildPayloadComplete&& OnComplete)
{
	// This requests the entire output to get one payload. It will be optimized later to request only one payload.
	TRequest Job(CreateBuildJob(Cache, BuildSystem, InputResolver, PayloadKey.BuildKey));
	FOnBuildJobComplete OnJobComplete;
	if (OnComplete)
	{
		OnJobComplete = [PayloadKey, OnComplete = MoveTemp(OnComplete)](FBuildJobCompleteParams&& Params)
		{
			FPayload Payload;
			EStatus Status = Params.Status;
			if (Status == EStatus::Ok)
			{
				Payload = Params.Output.GetPayload(PayloadKey.Id);
				Status = Payload ? EStatus::Ok : EStatus::Error;
			}
			if (!Payload)
			{
				Payload = FPayload(PayloadKey.Id);
			}
			OnComplete({PayloadKey.BuildKey, MoveTemp(Payload), Status});
		};
	}
	Job->Schedule(Scheduler, Policy, Priority, MoveTemp(OnJobComplete));
	return Job;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildSession CreateBuildSession(IBuildSessionInternal* Session)
{
	return FBuildSession(Session);
}

FBuildSession CreateBuildSession(
	FStringView Name,
	ICache& Cache,
	IBuild& BuildSystem,
	IBuildScheduler& Scheduler,
	IBuildInputResolver* InputResolver)
{
	return CreateBuildSession(new FBuildSessionInternal(Name, Cache, BuildSystem, Scheduler, InputResolver));
}

} // UE::DerivedData::Private
