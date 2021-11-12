// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassZoneGraphStandTask.h"
#include "StateTreeExecutionContext.h"
#include "ZoneGraphSubsystem.h"
#include "MassZoneGraphMovementFragments.h"
#include "MassAIBehaviorTypes.h"
#include "MassAIMovementFragments.h"
#include "MassMovementSettings.h"
#include "MassStateTreeExecutionContext.h"
#include "MassSignalSubsystem.h"
#include "MassSimulationLOD.h"
#include "MassZoneGraphMovementUtils.h"

bool FMassZoneGraphStandTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(LocationHandle);
	Linker.LinkExternalData(MoveTargetHandle);
	Linker.LinkExternalData(ShortPathHandle);
	Linker.LinkExternalData(CachedLaneHandle);
	Linker.LinkExternalData(ZoneGraphSubsystemHandle);
	Linker.LinkExternalData(MassSignalSubsystemHandle);
	Linker.LinkExternalData(MovementConfigHandle);

	Linker.LinkInstanceDataProperty(DurationHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassZoneGraphStandTaskInstanceData, Duration));
	Linker.LinkInstanceDataProperty(TimeHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassZoneGraphStandTaskInstanceData, Time));

	return true;
}

EStateTreeRunStatus FMassZoneGraphStandTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	// Do not reset of the state if current state is still active after transition, unless transitioned specifically to this state.
	if (ChangeType == EStateTreeStateChangeType::Sustained && Transition.Current != Transition.Next)
	{
		return EStateTreeRunStatus::Running;
	}

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);

	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalData(LocationHandle);
	const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetExternalData(ZoneGraphSubsystemHandle);
	const FMassMovementConfigFragment& MovementConfig = Context.GetExternalData(MovementConfigHandle);

	const UMassMovementSettings* Settings = GetDefault<UMassMovementSettings>();
	check(Settings);

	const FMassMovementConfig* Config = Settings->GetMovementConfigByHandle(MovementConfig.ConfigHandle);
	if (!Config)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Failed to get move config."));
		return EStateTreeRunStatus::Failed;
	}
	
	if (!LaneLocation.LaneHandle.IsValid())
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Invalid lande handle"));
		return EStateTreeRunStatus::Failed;
	}

	FMassZoneGraphShortPathFragment& ShortPath = Context.GetExternalData(ShortPathHandle);
	FMassZoneGraphCachedLaneFragment& CachedLane = Context.GetExternalData(CachedLaneHandle);
	FMassMoveTargetFragment& MoveTarget = Context.GetExternalData(MoveTargetHandle);

	// TODO: This could be smarter too, like having a stand location/direction, or even make a small path to stop, if we're currently running.

	const UWorld* World = Context.GetWorld();
	checkf(World != nullptr, TEXT("A valid world is expected from the execution context"));

	MoveTarget.CreateNewAction(EMassMovementAction::Stand, *World);
	const bool bSuccess = UE::MassMovement::ActivateActionStand(*World, Context.GetOwner(), MassContext.GetEntity(), ZoneGraphSubsystem, LaneLocation, Config->DefaultDesiredSpeed, MoveTarget, ShortPath, CachedLane);
	if (!bSuccess)
	{
		return EStateTreeRunStatus::Failed;
	}

	const float Duration = Context.GetInstanceData(DurationHandle);
	float& Time = Context.GetInstanceData(TimeHandle);
	Time = 0.0f;

	// A Duration <= 0 indicates that the task runs until a transition in the state tree stops it.
	// Otherwise we schedule a signal to end the task.
	if (Duration > 0.0f)
	{
		UMassSignalSubsystem& MassSignalSubsystem = Context.GetExternalData(MassSignalSubsystemHandle);
		MassSignalSubsystem.DelaySignalEntity(UE::Mass::Signals::StandTaskFinished, MassContext.GetEntity(), Duration);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMassZoneGraphStandTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const float Duration = Context.GetInstanceData(DurationHandle);
	float& Time = Context.GetInstanceData(TimeHandle);
	
	Time += DeltaTime;
	return Duration <= 0.0f ? EStateTreeRunStatus::Running : (Time < Duration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}
