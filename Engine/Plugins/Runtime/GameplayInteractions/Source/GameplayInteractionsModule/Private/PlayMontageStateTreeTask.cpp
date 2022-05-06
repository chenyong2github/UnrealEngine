// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayMontageStateTreeTask.h"
#include "StateTreeExecutionContext.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "StateTreeLinker.h"

struct FDataRegistryLookup;
struct FDataRegistryId;
struct FMassEntityHandle;

bool FPlayMontageStateTreeTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(ComputedDurationHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, ComputedDuration));
	Linker.LinkInstanceDataProperty(TimeHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Time));

	Linker.LinkExternalData(InteractorActorHandle);
	
	return true;
}

EStateTreeRunStatus FPlayMontageStateTreeTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	if (Montage == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* Interactor = Context.GetExternalDataPtr(InteractorActorHandle);
	ACharacter* Character = Cast<ACharacter>(Interactor);
	if (Character == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	float& Time = Context.GetInstanceData(TimeHandle);
	Time = 0.f;

	// Grab the task duration from the montage.
	float& ComputedDuration = Context.GetInstanceData(ComputedDurationHandle);
	ComputedDuration = Montage->GetPlayLength();

	Character->PlayAnimMontage(Montage);
	// @todo: listen anim completed event

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FPlayMontageStateTreeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const float ComputedDuration = Context.GetInstanceData(ComputedDurationHandle);
	float& Time = Context.GetInstanceData(TimeHandle);

	Time += DeltaTime;
	return ComputedDuration <= 0.0f ? EStateTreeRunStatus::Running : (Time < ComputedDuration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}