// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassCrowdClaimWaitSlotTask.h"
#include "StateTreeExecutionContext.h"
#include "MassCrowdSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassNavigationFragments.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTreeLinker.h"

FMassCrowdClaimWaitSlotTask::FMassCrowdClaimWaitSlotTask()
{
	// This task should not react to Enter/ExitState when the state is reselected.
	bShouldStateChangeOnReselect = false;
}

bool FMassCrowdClaimWaitSlotTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(LocationHandle);
	Linker.LinkExternalData(MoveTargetHandle);
	Linker.LinkExternalData(CrowdSubsystemHandle);

	Linker.LinkInstanceDataProperty(WaitSlotLocationHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassCrowdClaimWaitSlotTaskInstanceData, WaitSlotLocation));
	Linker.LinkInstanceDataProperty(WaitingSlotIndexHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassCrowdClaimWaitSlotTaskInstanceData, WaitingSlotIndex));
	Linker.LinkInstanceDataProperty(AcquiredLaneHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassCrowdClaimWaitSlotTaskInstanceData, AcquiredLane));

	return true;
}

EStateTreeRunStatus FMassCrowdClaimWaitSlotTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FMassZoneGraphTargetLocation& WaitSlotLocation = Context.GetInstanceData(WaitSlotLocationHandle);
	int32& WaitingSlotIndex = Context.GetInstanceData(WaitingSlotIndexHandle);
	FZoneGraphLaneHandle& AcquiredLane = Context.GetInstanceData(AcquiredLaneHandle);

	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassEntityHandle Entity = MassContext.GetEntity();
	
	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalData(LocationHandle);
	const FMassMoveTargetFragment& MoveTarget = Context.GetExternalData(MoveTargetHandle);
	UMassCrowdSubsystem& CrowdSubsystem = Context.GetExternalData(CrowdSubsystemHandle);

	FVector SlotPosition = FVector::ZeroVector;
	FVector SlotDirection = FVector::ForwardVector;
	WaitingSlotIndex = CrowdSubsystem.AcquireWaitingSlot(Entity, MoveTarget.Center, LaneLocation.LaneHandle, SlotPosition, SlotDirection);
	if (WaitingSlotIndex == INDEX_NONE)
	{
		// Failed to acquire slot
		return EStateTreeRunStatus::Failed;
	}
	
	AcquiredLane = LaneLocation.LaneHandle;

	WaitSlotLocation.LaneHandle = LaneLocation.LaneHandle;
	WaitSlotLocation.NextExitLinkType = EZoneLaneLinkType::None;
	WaitSlotLocation.NextLaneHandle.Reset();
	WaitSlotLocation.bMoveReverse = false;
	WaitSlotLocation.EndOfPathIntent = EMassMovementAction::Stand;
	WaitSlotLocation.EndOfPathPosition = SlotPosition;
	WaitSlotLocation.EndOfPathDirection = SlotDirection;
	WaitSlotLocation.TargetDistance = LaneLocation.LaneLength; // Go to end of lane
	// Let's start moving toward the interaction a bit before the entry point.
	WaitSlotLocation.AnticipationDistance.Set(100.f);
	
	return EStateTreeRunStatus::Running;
}

void FMassCrowdClaimWaitSlotTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassEntityHandle Entity = MassContext.GetEntity();

	UMassCrowdSubsystem& CrowdSubsystem = Context.GetExternalData(CrowdSubsystemHandle);

	FMassZoneGraphTargetLocation& WaitSlotLocation = Context.GetInstanceData(WaitSlotLocationHandle);
	int32& WaitingSlotIndex = Context.GetInstanceData(WaitingSlotIndexHandle);
	FZoneGraphLaneHandle& AcquiredLane = Context.GetInstanceData(AcquiredLaneHandle);
	
	if (WaitingSlotIndex != INDEX_NONE)
	{
		CrowdSubsystem.ReleaseWaitingSlot(Entity, AcquiredLane, WaitingSlotIndex);
	}
	
	WaitingSlotIndex = INDEX_NONE;
	AcquiredLane.Reset();
	WaitSlotLocation.Reset();
}
