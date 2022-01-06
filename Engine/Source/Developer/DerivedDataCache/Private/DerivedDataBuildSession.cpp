// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildSession.h"

#include "Containers/UnrealString.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildJob.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataValue.h"

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

	void BuildValue(
		const FBuildValueKey& Value,
		EBuildPolicy Policy,
		IRequestOwner& Owner,
		FOnBuildValueComplete&& OnComplete) final;

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

void FBuildSessionInternal::BuildValue(
	const FBuildValueKey& ValueKey,
	EBuildPolicy Policy,
	IRequestOwner& Owner,
	FOnBuildValueComplete&& OnComplete)
{
	// This requests the entire output to get one value. It will be optimized later to request only one value.
	FOnBuildComplete OnJobComplete;
	if (OnComplete)
	{
		OnJobComplete = [ValueKey, OnComplete = MoveTemp(OnComplete)](FBuildCompleteParams&& Params)
		{
			FValueWithId Value;
			EStatus Status = Params.Status;
			if (Status == EStatus::Ok)
			{
				Value = Params.Output.GetValue(ValueKey.Id);
				Status = Value ? EStatus::Ok : EStatus::Error;
			}
			if (!Value)
			{
				Value = FValueWithId(ValueKey.Id);
			}
			OnComplete({ValueKey.BuildKey, MoveTemp(Value), Status});
		};
	}
	CreateBuildJob({Cache, BuildSystem, Scheduler, InputResolver, Owner}, ValueKey.BuildKey, Policy, MoveTemp(OnJobComplete));
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
