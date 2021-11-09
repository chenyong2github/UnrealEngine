// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassCrowdClaimWaitSlotTask.h"
#include "StateTreeExecutionContext.h"
#include "MassCrowdSubsystem.h"
#include "MassZoneGraphMovementFragments.h"
#include "MassAIMovementFragments.h"
#include "MassStateTreeExecutionContext.h"

bool FMassCrowdClaimWaitSlotTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalItem(LocationHandle);
	Linker.LinkExternalItem(MoveTargetHandle);
	Linker.LinkExternalItem(CrowdSubsystemHandle);

	return true;
}

EStateTreeRunStatus FMassCrowdClaimWaitSlotTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	if (ChangeType != EStateTreeStateChangeType::Changed)
	{
		return EStateTreeRunStatus::Running;
	}

	WaitSlotLocation = nullptr;

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassEntityHandle Entity = MassContext.GetEntity();
	
	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalItem(LocationHandle);
	const FMassMoveTargetFragment& MoveTarget = Context.GetExternalItem(MoveTargetHandle);
	UMassCrowdSubsystem& CrowdSubsystem = Context.GetExternalItem(CrowdSubsystemHandle);

	FVector SlotPosition = FVector::ZeroVector;
	FVector SlotDirection = FVector::ForwardVector;
	WaitingSlotIndex = CrowdSubsystem.AcquireWaitingSlot(Entity, MoveTarget.Center, LaneLocation.LaneHandle, SlotPosition, SlotDirection);
	if (WaitingSlotIndex == INDEX_NONE)
	{
		// Failed to acquire slot
		return EStateTreeRunStatus::Failed;
	}
	
	AcquiredLaneHandle = LaneLocation.LaneHandle;

	TargetLocation.LaneHandle = LaneLocation.LaneHandle;
	TargetLocation.NextExitLinkType = EZoneLaneLinkType::None;
	TargetLocation.NextLaneHandle.Reset();
	TargetLocation.bMoveReverse = false;
	TargetLocation.EndOfPathIntent = EMassMovementAction::Stand;
	TargetLocation.EndOfPathPosition = SlotPosition;
	TargetLocation.EndOfPathDirection = SlotDirection;
	TargetLocation.TargetDistance = LaneLocation.LaneLength; // Go to end of lane
	// Let's start moving toward the interaction a bit before the entry point.
	TargetLocation.AnticipationDistance.Set(100.f);

	WaitSlotLocation = &TargetLocation;
	
	return EStateTreeRunStatus::Running;
}

void FMassCrowdClaimWaitSlotTask::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	WaitSlotLocation = &TargetLocation;
	
	if (ChangeType != EStateTreeStateChangeType::Changed)
	{
		return;
	}

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassEntityHandle Entity = MassContext.GetEntity();
	
	UMassCrowdSubsystem& CrowdSubsystem = Context.GetExternalItem(CrowdSubsystemHandle);

	if (WaitingSlotIndex != INDEX_NONE)
	{
		CrowdSubsystem.ReleaseWaitingSlot(Entity, AcquiredLaneHandle, WaitingSlotIndex);
	}
	
	TargetLocation.Reset();
}

EStateTreeRunStatus FMassCrowdClaimWaitSlotTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	WaitSlotLocation = &TargetLocation;

	return EStateTreeRunStatus::Running;
}
