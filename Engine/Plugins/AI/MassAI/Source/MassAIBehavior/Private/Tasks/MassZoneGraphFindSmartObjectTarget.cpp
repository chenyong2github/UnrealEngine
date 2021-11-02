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
	Linker.LinkExternalItem(SmartObjectUserHandle);
	Linker.LinkExternalItem(LocationHandle);
	Linker.LinkExternalItem(AnnotationSubsystemHandle);

	return true;
}

EStateTreeRunStatus FMassZoneGraphFindSmartObjectTarget::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	const UZoneGraphAnnotationSubsystem& AnnotationSubsystem = Context.GetExternalItem(AnnotationSubsystemHandle);
	const FMassSmartObjectUserFragment& SOUser = Context.GetExternalItem(SmartObjectUserHandle);
	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalItem(LocationHandle);
	const FZoneGraphLaneHandle LaneHandle(LaneLocation.LaneHandle);

	TargetLocationRef = nullptr;

	if (Context.GetEnterStateStatus() == EStateTreeRunStatus::Failed)
	{
		MASSBEHAVIOR_LOG(Log, TEXT("Transition to the state has been denied by other tasks. Nothing to do."));
		return EStateTreeRunStatus::Failed;
	}

	if (!SOUser.ClaimHandle.SmartObjectID.IsValid())
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

	TargetLocation.LaneHandle = LaneHandle;
	TargetLocation.NextExitLinkType = EZoneLaneLinkType::None;
	TargetLocation.NextLaneHandle.Reset();
	TargetLocation.bMoveReverse = false;
	TargetLocation.EndOfPathIntent = EMassMovementAction::Stand;
	TargetLocation.EndOfPathPosition = SOUser.GetTargetLocation();
	// Let's start moving toward the interaction a bit before the entry point.
	constexpr float AnticipationDistance = 100.f;
	TargetLocation.AnticipationDistance = AnticipationDistance;

	if (SOAnnotations != nullptr)
	{
		// Find entry point on lanes for the claimed object
		const FSmartObjectAnnotationData* AnnotationData = SOAnnotations->GetAnnotationData(LaneHandle.DataHandle);
		checkf(AnnotationData, TEXT("FSmartObjectAnnotationData should have been created for each registered valid ZoneGraphData"));
		const FSmartObjectLaneLocation EntryPoint = AnnotationData->ObjectToEntryPointLookup.FindChecked(SOUser.ClaimHandle.SmartObjectID);

		// Request path along current lane to reach entry point on lane
		MASSBEHAVIOR_LOG(Log, TEXT("Claim successful: create path along lane to reach interaction location."));
		TargetLocation.TargetDistance = EntryPoint.DistanceAlongLane;
	}
	else
	{
		// Request path from current lane location directly to interaction location
		MASSBEHAVIOR_LOG(Warning, TEXT("Claim successful: create path from current lane location directly to interaction location since SmartObject zone annotations weren't found."));
		TargetLocation.TargetDistance = LaneLocation.DistanceAlongLane;
	}

	TargetLocationRef = &TargetLocation;
	
	return EStateTreeRunStatus::Running;
}

void FMassZoneGraphFindSmartObjectTarget::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	// Temp solution to make sure the target location is always up to date (will be replaced with automatic update).
	TargetLocationRef = &TargetLocation;
}

EStateTreeRunStatus FMassZoneGraphFindSmartObjectTarget::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	// Temp solution to make sure the target location is always up to date (will be replaced with automatic update).
	TargetLocationRef = &TargetLocation;
	
	return EStateTreeRunStatus::Running;
}
