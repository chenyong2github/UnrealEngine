// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassLookAtTask.h"
#include "MassAIBehaviorTypes.h"
#include "MassLookAtFragments.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeExecutionContext.h"

bool FMassLookAtTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(MassSignalSubsystemHandle);
	Linker.LinkExternalData(LookAtHandle);

	Linker.LinkInstanceDataProperty(DurationHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassLookAtTaskInstanceData, Duration));
	Linker.LinkInstanceDataProperty(TargetEntityHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassLookAtTaskInstanceData, TargetEntity));
	Linker.LinkInstanceDataProperty(TimeHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassLookAtTaskInstanceData, Time));
	
	return true;
}

EStateTreeRunStatus FMassLookAtTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	float& Time = Context.GetInstanceData(TimeHandle);

	Time = 0.f;
	
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassLookAtFragment& LookAtFragment = MassContext.GetExternalData(LookAtHandle);

	LookAtFragment.Reset();
	LookAtFragment.LookAtMode = LookAtMode;
	
	if (LookAtMode == EMassLookAtMode::LookAtEntity)
	{
		const FMassEntityHandle* TargetEntity = Context.GetInstanceDataPtr(TargetEntityHandle); // Optional input
		if (TargetEntity == nullptr || !TargetEntity->IsSet())
		{
			LookAtFragment.LookAtMode = EMassLookAtMode::LookForward;
			MASSBEHAVIOR_LOG(Error, TEXT("Failed LookAt: invalid target entity"));
		}
		else
		{
			LookAtFragment.LookAtMode = EMassLookAtMode::LookAtEntity;
			LookAtFragment.TrackedEntity = *TargetEntity;
		}
	}

	LookAtFragment.RandomGazeMode = RandomGazeMode;
	LookAtFragment.RandomGazeYawVariation = RandomGazeYawVariation;
	LookAtFragment.RandomGazePitchVariation = RandomGazePitchVariation;
	LookAtFragment.bRandomGazeEntities = bRandomGazeEntities;

	// A Duration <= 0 indicates that the task runs until a transition in the state tree stops it.
	// Otherwise we schedule a signal to end the task.
	const float Duration = Context.GetInstanceData(DurationHandle);
	if (Duration > 0.0f)
	{
		UMassSignalSubsystem& MassSignalSubsystem = MassContext.GetExternalData(MassSignalSubsystemHandle);
		MassSignalSubsystem.DelaySignalEntity(UE::Mass::Signals::LookAtFinished, MassContext.GetEntity(), Duration);
	}

	return EStateTreeRunStatus::Running;
}

void FMassLookAtTask::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassLookAtFragment& LookAtFragment = MassContext.GetExternalData(LookAtHandle);
	
	LookAtFragment.Reset();
}

EStateTreeRunStatus FMassLookAtTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	float& Time = Context.GetInstanceData(TimeHandle);
	const float Duration = Context.GetInstanceData(DurationHandle);
	
	Time += DeltaTime;
	
	return Duration <= 0.0f ? EStateTreeRunStatus::Running : (Time < Duration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}
