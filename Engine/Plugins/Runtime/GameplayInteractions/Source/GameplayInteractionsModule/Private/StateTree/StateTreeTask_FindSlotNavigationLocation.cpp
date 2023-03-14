// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTask_FindSlotNavigationLocation.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTreeLinker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTask_FindSlotNavigationLocation)

FStateTreeTask_FindSlotNavigationLocation::FStateTreeTask_FindSlotNavigationLocation()
{
	// No tick needed.
	bShouldCallTick = false;
	// No need to update bound properties after enter state.
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

bool FStateTreeTask_FindSlotNavigationLocation::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

bool FStateTreeTask_FindSlotNavigationLocation::UpdateResult(const FStateTreeExecutionContext& Context) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.ReferenceSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[StateTreeTask_FindSlotNavigationLocation] Expected valid ReferenceSlot handle."));
		return false;
	}

	if (!InstanceData.UserActor)
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[StateTreeTask_FindSlotNavigationLocation] Expected valid UserActor handle."));
		return false;
	}

	FSmartObjectSlotEntranceLocationRequest Request;
	Request.SetNavigationDataFromActor(*InstanceData.UserActor, ValidationFilter);
	Request.SelectMethod = SelectMethod;
	Request.bProjectNavigationLocation = bProjectNavigationLocation;
	Request.bTraceGroundLocation = bTraceGroundLocation;
	Request.bCheckTransitionTrajectory = bCheckTransitionTrajectory;
	Request.LocationType = LocationType;

	FSmartObjectSlotNavigationLocationResult EntryLocation;
	if (SmartObjectSubsystem.FindNavigationLocationForSlot(InstanceData.ReferenceSlot, Request, EntryLocation))
	{
		InstanceData.EntryTransform = FTransform(EntryLocation.Rotation, EntryLocation.Location);
		InstanceData.EntryTag = EntryLocation.Tag;
		return true;
	}

	return false;
}

EStateTreeRunStatus FStateTreeTask_FindSlotNavigationLocation::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (!UpdateResult(Context))
	{
		return EStateTreeRunStatus::Failed;
	}
	
	return EStateTreeRunStatus::Running;
}
