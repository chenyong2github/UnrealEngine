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

	void Build(
		const FBuildDefinition& Definition,
		EBuildPolicy Policy,
		IRequestOwner& Owner,
		FOnBuildComplete&& OnComplete) final;

	void BuildAction(
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		EBuildPolicy Policy,
		IRequestOwner& Owner,
		FOnBuildActionComplete&& OnComplete) final;

	void BuildPayload(
		const FBuildPayloadKey& Payload,
		EBuildPolicy Policy,
		IRequestOwner& Owner,
		FOnBuildPayloadComplete&& OnComplete) final;

	FString Name;
	ICache& Cache;
	IBuild& BuildSystem;
	IBuildScheduler& Scheduler;
	IBuildInputResolver* InputResolver;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildSessionInternal::Build(
	const FBuildDefinition& Definition,
	EBuildPolicy Policy,
	IRequestOwner& Owner,
	FOnBuildComplete&& OnComplete)
{
	FOnBuildJobComplete OnJobComplete;
	if (OnComplete)
	{
		OnJobComplete = [Definition, OnComplete = MoveTemp(OnComplete)](FBuildJobCompleteParams&& Params)
		{
			OnComplete({Definition.GetKey(), Params.CacheKey, MoveTemp(Params.Output), Params.BuildStatus, Params.Status});
		};
	}
	CreateBuildJob({Cache, BuildSystem, Scheduler, InputResolver, Owner, Policy}, Definition, MoveTemp(OnJobComplete));
}

void FBuildSessionInternal::BuildAction(
	const FBuildAction& Action,
	const FOptionalBuildInputs& Inputs,
	EBuildPolicy Policy,
	IRequestOwner& Owner,
	FOnBuildActionComplete&& OnComplete)
{
	FOnBuildJobComplete OnJobComplete;
	if (OnComplete)
	{
		OnJobComplete = [Action, OnComplete = MoveTemp(OnComplete)](FBuildJobCompleteParams&& Params)
		{
			OnComplete({Action.GetKey(), Params.CacheKey, MoveTemp(Params.Output), Params.BuildStatus, Params.Status});
		};
	}
	CreateBuildJob({Cache, BuildSystem, Scheduler, InputResolver, Owner, Policy}, Action, Inputs, MoveTemp(OnJobComplete));
}

void FBuildSessionInternal::BuildPayload(
	const FBuildPayloadKey& PayloadKey,
	EBuildPolicy Policy,
	IRequestOwner& Owner,
	FOnBuildPayloadComplete&& OnComplete)
{
	// This requests the entire output to get one payload. It will be optimized later to request only one payload.
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
	CreateBuildJob({Cache, BuildSystem, Scheduler, InputResolver, Owner, Policy}, PayloadKey.BuildKey, MoveTemp(OnJobComplete));
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
