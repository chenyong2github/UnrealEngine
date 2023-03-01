// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTask_FindSlotEntryForActor.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTreeLinker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTask_FindSlotEntryForActor)

FStateTreeTask_FindSlotEntryForActor::FStateTreeTask_FindSlotEntryForActor()
{
	// No tick needed.
	bShouldCallTick = false;
	// No need to update bound properties after enter state.
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

bool FStateTreeTask_FindSlotEntryForActor::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

bool FStateTreeTask_FindSlotEntryForActor::UpdateResult(const FStateTreeExecutionContext& Context) const
{
	const USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.ReferenceSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[StateTreeTask_FindSlotEntryForActor] Expected valid ReferenceSlot handle."));
		return false;
	}

	if (!InstanceData.UserActor)
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[StateTreeTask_FindSlotEntryForActor] Expected valid UserActor handle."));
		return false;
	}

	FSmartObjectSlotEntryRequest Request;
	Request.SetNavigationDataFromActor(*InstanceData.UserActor, NavigationFilter);
	Request.SelectMethod = SelectMethod;
	Request.bRequireResultInNavigableSpace = bRequireResultInNavigableSpace;
	Request.bIncludeEntriesAsCandidates = bIncludeEntriesAsCandidates;
	Request.bIncludeExistsAsCandidates = bIncludeExistsAsCandidates;

	FSmartObjectSlotEntryLocationResult EntryLocation;
	if (SmartObjectSubsystem.FindEntryLocationForSlot(InstanceData.ReferenceSlot, Request, EntryLocation))
	{
		FQuat Rotation = EntryLocation.Rotation.Quaternion();
		if (!ResultRotationAdjustment.IsNearlyZero())
		{
			Rotation = FQuat(ResultRotationAdjustment.Quaternion()) * Rotation;
		}
		InstanceData.EntryTransform = FTransform(Rotation, EntryLocation.Location);
		InstanceData.EntryTag = EntryLocation.Tag;
		return true;
	}

	return false;
}

EStateTreeRunStatus FStateTreeTask_FindSlotEntryForActor::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (!UpdateResult(Context))
	{
		return EStateTreeRunStatus::Failed;
	}
	
	return EStateTreeRunStatus::Running;
}
