// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassZoneGraphFindSmartObjectTarget.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "MassAIBehaviorTypes.h"
#include "MassSmartObjectProcessor.h"
#include "MassSmartObjectSettings.h"
#include "MassStateTreeExecutionContext.h"
#include "MassZoneGraphMovementFragments.h"
#include "SmartObjectZoneAnnotations.h"

bool FMassZoneGraphFindSmartObjectTarget::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectUserHandle);
	Linker.LinkExternalData(LocationHandle);
	Linker.LinkExternalData(AnnotationSubsystemHandle);

	Linker.LinkInstanceDataProperty(SmartObjectLocationHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassZoneGraphFindSmartObjectTargetInstanceData, SmartObjectLocation));
	
	return true;
}

EStateTreeRunStatus FMassZoneGraphFindSmartObjectTarget::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	const UZoneGraphAnnotationSubsystem& AnnotationSubsystem = Context.GetExternalData(AnnotationSubsystemHandle);
	const FMassSmartObjectUserFragment& SOUser = Context.GetExternalData(SmartObjectUserHandle);
	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalData(LocationHandle);
	const FZoneGraphLaneHandle LaneHandle(LaneLocation.LaneHandle);

	FMassZoneGraphTargetLocation& SmartObjectLocation = Context.GetInstanceData(SmartObjectLocationHandle);
	SmartObjectLocation.Reset();

	if (!SOUser.ClaimHandle.SmartObjectHandle.IsValid())
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Invalid claimed smart object ID."));
		return EStateTreeRunStatus::Failed;
	}

	if (!LaneHandle.IsValid())
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Invalid lane location."));
		return EStateTreeRunStatus::Failed;
	}

	const FZoneGraphTag SmartObjectTag = GetDefault<UMassSmartObjectSettings>()->SmartObjectTag;
	const USmartObjectZoneAnnotations* SOAnnotations = Cast<USmartObjectZoneAnnotations>(AnnotationSubsystem.GetFirstAnnotationForTag(SmartObjectTag));

	
	SmartObjectLocation.LaneHandle = LaneHandle;
	SmartObjectLocation.NextExitLinkType = EZoneLaneLinkType::None;
	SmartObjectLocation.NextLaneHandle.Reset();
	SmartObjectLocation.bMoveReverse = false;
	SmartObjectLocation.EndOfPathIntent = EMassMovementAction::Stand;
	SmartObjectLocation.EndOfPathPosition = SOUser.GetTargetLocation();
	// Let's start moving toward the interaction a bit before the entry point.
	SmartObjectLocation.AnticipationDistance.Set(100.f);

	if (SOAnnotations != nullptr)
	{
		// Find entry point on lanes for the claimed object
		const FSmartObjectAnnotationData* AnnotationData = SOAnnotations->GetAnnotationData(LaneHandle.DataHandle);
		checkf(AnnotationData, TEXT("FSmartObjectAnnotationData should have been created for each registered valid ZoneGraphData"));
		const FSmartObjectLaneLocation EntryPoint = AnnotationData->ObjectToEntryPointLookup.FindChecked(SOUser.ClaimHandle.SmartObjectHandle);

		// Request path along current lane to reach entry point on lane
		MASSBEHAVIOR_LOG(Log, TEXT("Claim successful: create path along lane to reach interaction location."));
		SmartObjectLocation.TargetDistance = EntryPoint.DistanceAlongLane;
	}
	else
	{
		// Request path from current lane location directly to interaction location
		MASSBEHAVIOR_LOG(Warning, TEXT("Claim successful: create path from current lane location directly to interaction location since SmartObject zone annotations weren't found."));
		SmartObjectLocation.TargetDistance = LaneLocation.DistanceAlongLane;
	}
	
	return EStateTreeRunStatus::Running;
}
