// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildSession.h"

#include "Containers/UnrealString.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildJob.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataPayload.h"

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
		const FOptionalBuildInputs& Inputs,
		const FBuildPolicy& Policy,
		IRequestOwner& Owner,
		FOnBuildComplete&& OnComplete) final;

	void Build(
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		const FBuildPolicy& Policy,
		IRequestOwner& Owner,
		FOnBuildComplete&& OnComplete) final;

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
	const FOptionalBuildInputs& Inputs,
	const FBuildPolicy& Policy,
	IRequestOwner& Owner,
	FOnBuildComplete&& OnComplete)
{
	CreateBuildJob({Cache, BuildSystem, Scheduler, InputResolver, Owner}, Definition, Inputs, Policy,
		OnComplete ? MoveTemp(OnComplete) : [](FBuildCompleteParams&&){});
}

void FBuildSessionInternal::Build(
	const FBuildAction& Action,
	const FOptionalBuildInputs& Inputs,
	const FBuildPolicy& Policy,
	IRequestOwner& Owner,
	FOnBuildComplete&& OnComplete)
{
	CreateBuildJob({Cache, BuildSystem, Scheduler, InputResolver, Owner}, Action, Inputs, Policy,
		OnComplete ? MoveTemp(OnComplete) : [](FBuildCompleteParams&&){});
}

void FBuildSessionInternal::BuildPayload(
	const FBuildPayloadKey& PayloadKey,
	EBuildPolicy Policy,
	IRequestOwner& Owner,
	FOnBuildPayloadComplete&& OnComplete)
{
	// This requests the entire output to get one payload. It will be optimized later to request only one payload.
	FOnBuildComplete OnJobComplete;
	if (OnComplete)
	{
		OnJobComplete = [PayloadKey, OnComplete = MoveTemp(OnComplete)](FBuildCompleteParams&& Params)
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
	CreateBuildJob({Cache, BuildSystem, Scheduler, InputResolver, Owner}, PayloadKey.BuildKey, Policy, MoveTemp(OnJobComplete));
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
