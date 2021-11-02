// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassStateTreeSmartObjectTask.h"
#include "MassCommonFragments.h"
#include "MassAIBehaviorTypes.h"
#include "MassSmartObjectHandler.h"
#include "MassSmartObjectProcessor.h"
#include "MassStateTreeExecutionContext.h"
#include "MassAIMovementFragments.h"
#include "SmartObjectSubsystem.h"
#include "MassZoneGraphMovementUtils.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
// FMassClaimSmartObjectTask
//----------------------------------------------------------------------//

bool FMassClaimSmartObjectTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalItem(SmartObjectUserHandle);
	Linker.LinkExternalItem(SmartObjectSubsystemHandle);

	return true;
}

EStateTreeRunStatus FMassClaimSmartObjectTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	if (ChangeType != EStateTreeStateChangeType::Changed)
	{
		return EStateTreeRunStatus::Running;
	}

	if (!SearchRequestResult.bProcessed)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Failed to claim smart object: unprocessed search request results."));
		return EStateTreeRunStatus::Failed;
	}

	if (ClaimResult != EMassSmartObjectClaimResult::Unset)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Claim result should be 'Unset' but is '%s'"), *UEnum::GetValueAsString(ClaimResult));
		return EStateTreeRunStatus::Failed;
	}

	EStateTreeRunStatus Status = EStateTreeRunStatus::Failed;

	// Retrieve fragments and subsystems
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalItem(SmartObjectSubsystemHandle);
	FMassSmartObjectUserFragment& SOUser = Context.GetExternalItem(SmartObjectUserHandle);

	// Setup MassSmartObject handler and claim
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassSmartObjectHandler MassSmartObjectHandler(MassContext.GetEntitySubsystem(), MassContext.GetEntitySubsystemExecutionContext(), SmartObjectSubsystem);
	ClaimResult = MassSmartObjectHandler.ClaimCandidate(MassContext.GetEntity(), SOUser, SearchRequestResult);

	switch (ClaimResult)
	{
	case EMassSmartObjectClaimResult::Succeeded:
		Status = EStateTreeRunStatus::Running;
		break;
	case EMassSmartObjectClaimResult::Failed_InvalidRequest:
		MASSBEHAVIOR_LOG(Error, TEXT("Failed to claim smart object: %s"), *UEnum::GetValueAsString(ClaimResult));
		break;
	case EMassSmartObjectClaimResult::Failed_NoAvailableCandidate:
		MASSBEHAVIOR_LOG(Log, TEXT("Failed to claim smart object: %s"), *UEnum::GetValueAsString(ClaimResult));
		break;
	case EMassSmartObjectClaimResult::Failed_UnprocessedRequest:
		MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Failed to claim smart object: %s"), *UEnum::GetValueAsString(ClaimResult));
		break;
	default:
		ensureMsgf(false, TEXT("Unsupported EMassSmartObjectClaimResult value: %d"), ClaimResult);
	}

	return Status;
}

void FMassClaimSmartObjectTask::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	if (ChangeType != EStateTreeStateChangeType::Changed)
	{
		return;
	}

	ClaimResult = EMassSmartObjectClaimResult::Unset;

	FMassSmartObjectUserFragment& SOUser = Context.GetExternalItem(SmartObjectUserHandle);

	if (SOUser.ClaimHandle.IsValid())
	{
		if (SOUser.InteractionStatus == EMassSmartObjectInteractionStatus::Unset)
		{
			MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Exiting state with a valid claim handle but interaction was not started: release claim on the smart object."));
			const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
			USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalItem(SmartObjectSubsystemHandle);
			const FMassSmartObjectHandler MassSmartObjectHandler(MassContext.GetEntitySubsystem(), MassContext.GetEntitySubsystemExecutionContext(), SmartObjectSubsystem);
			MassSmartObjectHandler.ReleaseSmartObject(MassContext.GetEntity(), SOUser);
		}
		else
		{
			MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Exiting state with a valid claim handle and interaction was started: nothing to do since use task will stop the interaction."));
		}
	}
	else
	{
		MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Exiting state with an invalid ClaimHandle: nothing to do."));
	}
}

EStateTreeRunStatus FMassClaimSmartObjectTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	return EStateTreeRunStatus::Running;
}

//----------------------------------------------------------------------//
// FMassUseSmartObjectTask
//----------------------------------------------------------------------//

bool FMassUseSmartObjectTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalItem(SmartObjectSubsystemHandle);
	Linker.LinkExternalItem(EntityTransformHandle);
	Linker.LinkExternalItem(SmartObjectUserHandle);
	Linker.LinkExternalItem(MoveTargetHandle);

	return true;
}

EStateTreeRunStatus FMassUseSmartObjectTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	if (ChangeType != EStateTreeStateChangeType::Changed)
	{
		return EStateTreeRunStatus::Running;
	}

	// Retrieve fragments and subsystems
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalItem(SmartObjectSubsystemHandle);
	FMassSmartObjectUserFragment& SOUser = Context.GetExternalItem(SmartObjectUserHandle);
	const FDataFragment_Transform& TransformFragment = Context.GetExternalItem(EntityTransformHandle);
	FMassMoveTargetFragment& MoveTarget = Context.GetExternalItem(MoveTargetHandle);

	// Setup MassSmartObject handler and start interaction
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassSmartObjectHandler MassSmartObjectHandler(MassContext.GetEntitySubsystem(), MassContext.GetEntitySubsystemExecutionContext(), SmartObjectSubsystem);

	if (!MassSmartObjectHandler.UseSmartObject(MassContext.GetEntity(), SOUser, TransformFragment))
	{
		return EStateTreeRunStatus::Failed;
	}

	// @todo: we should have common API to control this, currently handle via tasks.
	const UWorld* World = Context.GetWorld();
	checkf(World != nullptr, TEXT("A valid world is expected from the execution context"));

	MoveTarget.CreateNewAction(EMassMovementAction::Animate, *World);
	const bool bSuccess = UE::MassMovement::ActivateActionAnimate(*World, Context.GetOwner(), MassContext.GetEntity(), MoveTarget);

	return bSuccess ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

void FMassUseSmartObjectTask::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	if (ChangeType == EStateTreeStateChangeType::Changed)
	{
		FMassSmartObjectUserFragment& SOUser = Context.GetExternalItem(SmartObjectUserHandle);

		if (SOUser.ClaimHandle.IsValid())
		{
			MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Exiting state with a valid ClaimHandle: stop using the smart object."));

			const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
			USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalItem(SmartObjectSubsystemHandle);
			const FMassSmartObjectHandler MassSmartObjectHandler(MassContext.GetEntitySubsystem(), MassContext.GetEntitySubsystemExecutionContext(), SmartObjectSubsystem);
			MassSmartObjectHandler.ReleaseSmartObject(MassContext.GetEntity(), SOUser, EMassSmartObjectInteractionStatus::Aborted);
		}
		else
		{
			MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Exiting state with an invalid ClaimHandle: nothing to do."));
		}
	}
}

void FMassUseSmartObjectTask::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState)
{
	FMassSmartObjectUserFragment& SOUser = Context.GetExternalItem(SmartObjectUserHandle);

	if (SOUser.ClaimHandle.IsValid())
	{
		MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Completing state with a valid ClaimHandle: stop using the smart object."));

		const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
		USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalItem(SmartObjectSubsystemHandle);
		const FMassSmartObjectHandler MassSmartObjectHandler(MassContext.GetEntitySubsystem(), MassContext.GetEntitySubsystemExecutionContext(), SmartObjectSubsystem);
		const EMassSmartObjectInteractionStatus NewStatus = CompletionStatus == EStateTreeRunStatus::Succeeded ? EMassSmartObjectInteractionStatus::Completed : EMassSmartObjectInteractionStatus::Aborted;
		MassSmartObjectHandler.ReleaseSmartObject(MassContext.GetEntity(), SOUser, NewStatus);
	}
}

EStateTreeRunStatus FMassUseSmartObjectTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	EStateTreeRunStatus Status = EStateTreeRunStatus::Failed;

	FMassSmartObjectUserFragment& SOUser = Context.GetExternalItem(SmartObjectUserHandle);
	switch (SOUser.InteractionStatus)
	{
	case EMassSmartObjectInteractionStatus::InProgress:
		MASSBEHAVIOR_LOG(VeryVerbose, TEXT("Interacting ..."));
		Status = EStateTreeRunStatus::Running;
		break;

	case EMassSmartObjectInteractionStatus::Completed:
		MASSBEHAVIOR_LOG(Log, TEXT("Interaction completed"));
		SOUser.SetCooldown(Context.GetWorld()->GetTimeSeconds() + Cooldown);
		Status = EStateTreeRunStatus::Succeeded;
		break;

	case EMassSmartObjectInteractionStatus::Aborted:
		MASSBEHAVIOR_LOG(Log, TEXT("Interaction aborted"));
		Status = EStateTreeRunStatus::Failed;
		break;

	case EMassSmartObjectInteractionStatus::Unset:
		MASSBEHAVIOR_LOG(Error, TEXT("Error while using  smart object: interaction state is not valid"));
		Status = EStateTreeRunStatus::Failed;
		break;

	default:
		ensureMsgf(false, TEXT("Unhandled interaction status % => Returning EStateTreeRunStatus::Failed"),
			*StaticEnum<EMassSmartObjectInteractionStatus>()->GetValueAsString(SOUser.InteractionStatus));
		Status = EStateTreeRunStatus::Failed;
	}

	return Status;
}
