// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassLookAtTask.h"
#include "MassAIBehaviorTypes.h"
#include "MassLookAtFragments.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeExecutionContext.h"

bool FMassLookAtTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalItem(MassSignalSubsystemHandle);
	Linker.LinkExternalItem(LookAtHandle);

	return true;
}

EStateTreeRunStatus FMassLookAtTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	Time = 0.f;
	
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassLookAtFragment& LookAtFragment = MassContext.GetExternalItem(LookAtHandle);

	LookAtFragment.Reset();
	LookAtFragment.LookAtMode = LookAtMode;
	
	if (LookAtMode == EMassLookAtMode::LookAtEntity)
	{
		if (!TargetEntity.IsSet())
		{
			LookAtFragment.LookAtMode = EMassLookAtMode::LookForward;
			MASSBEHAVIOR_LOG(Error, TEXT("Failed LookAt: invalid target entity"));
		}
		else
		{
			LookAtFragment.LookAtMode = EMassLookAtMode::LookAtEntity;
			LookAtFragment.TrackedEntity = TargetEntity;
		}
	}

	LookAtFragment.RandomGazeMode = RandomGazeMode;
	LookAtFragment.RandomGazeYawVariation = RandomGazeYawVariation;
	LookAtFragment.RandomGazePitchVariation = RandomGazePitchVariation;
	LookAtFragment.bRandomGazeEntities = bRandomGazeEntities;

	// A Duration <= 0 indicates that the task runs until a transition in the state tree stops it.
	// Otherwise we schedule a signal to end the task.
	if (Duration > 0.0f)
	{
		UMassSignalSubsystem& MassSignalSubsystem = MassContext.GetExternalItem(MassSignalSubsystemHandle);
		MassSignalSubsystem.DelaySignalEntity(UE::Mass::Signals::LookAtFinished, MassContext.GetEntity(), Duration);
	}

	return EStateTreeRunStatus::Running;
}

void FMassLookAtTask::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	FMassLookAtFragment& LookAtFragment = MassContext.GetExternalItem(LookAtHandle);
	
	LookAtFragment.Reset();
}

EStateTreeRunStatus FMassLookAtTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	Time += DeltaTime;
	return Duration <= 0.0f ? EStateTreeRunStatus::Running : (Time < Duration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}
