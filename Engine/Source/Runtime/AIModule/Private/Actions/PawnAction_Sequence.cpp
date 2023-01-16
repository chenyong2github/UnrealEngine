// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actions/PawnAction_Sequence.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "VisualLogger/VisualLogger.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PawnAction_Sequence)

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UDEPRECATED_UPawnAction_Sequence::UDEPRECATED_UPawnAction_Sequence(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SubActionTriggeringPolicy(EPawnSubActionTriggeringPolicy::CopyBeforeTriggering)
{
}

UDEPRECATED_UPawnAction_Sequence* UDEPRECATED_UPawnAction_Sequence::CreateAction(UWorld& World, TArray<UDEPRECATED_UPawnAction*>& ActionSequence, EPawnSubActionTriggeringPolicy::Type InSubActionTriggeringPolicy)
{
	ActionSequence.Remove(NULL);
	if (ActionSequence.Num() <= 0)
	{
		return NULL;
	}

	UDEPRECATED_UPawnAction_Sequence* Action = UDEPRECATED_UPawnAction::CreateActionInstance<UDEPRECATED_UPawnAction_Sequence>(World);
	if (Action)
	{
		Action->ActionSequence_DEPRECATED = ActionSequence;

		for (const UDEPRECATED_UPawnAction* SubAction : ActionSequence)
		{
			if (SubAction && SubAction->ShouldPauseMovement())
			{
				Action->bShouldPauseMovement = true;
				break;
			}
		}

		Action->SubActionTriggeringPolicy = InSubActionTriggeringPolicy;
	}

	return Action;
}

bool UDEPRECATED_UPawnAction_Sequence::Start()
{
	bool bResult = Super::Start();

	if (bResult)
	{
		UE_VLOG(GetPawn(), LogPawnAction, Log, TEXT("Starting sequence. Items:"), *GetName());
		for (auto Action : ActionSequence_DEPRECATED)
		{
			UE_VLOG(GetPawn(), LogPawnAction, Log, TEXT("    %s"), *GetNameSafe(Action));
		}

		bResult = PushNextActionCopy();
	}

	return bResult;
}

bool UDEPRECATED_UPawnAction_Sequence::Resume()
{
	bool bResult = Super::Resume();

	if (bResult)
	{
		bResult = PushNextActionCopy();
	}

	return bResult;
}

void UDEPRECATED_UPawnAction_Sequence::OnChildFinished(UDEPRECATED_UPawnAction& Action, EPawnActionResult::Type WithResult)
{
	if (RecentActionCopy_DEPRECATED == &Action)
	{
		if (WithResult == EPawnActionResult::Success || (WithResult == EPawnActionResult::Failed && ChildFailureHandlingMode == EPawnActionFailHandling::IgnoreFailure))
		{
			if (GetAbortState() == EPawnActionAbortState::NotBeingAborted)
			{
				PushNextActionCopy();
			}
		}
		else
		{
			Finish(EPawnActionResult::Failed);
		}
	}

	Super::OnChildFinished(Action, WithResult);
}

bool UDEPRECATED_UPawnAction_Sequence::PushNextActionCopy()
{
	if (CurrentActionIndex >= uint32(ActionSequence_DEPRECATED.Num()))
	{
		Finish(EPawnActionResult::Success);
		return true;
	}

	UDEPRECATED_UPawnAction* ActionCopy = SubActionTriggeringPolicy == EPawnSubActionTriggeringPolicy::CopyBeforeTriggering
		? Cast<UDEPRECATED_UPawnAction>(StaticDuplicateObject(ActionSequence_DEPRECATED[CurrentActionIndex], this))
		: ToRawPtr(ActionSequence_DEPRECATED[CurrentActionIndex]);

	UE_VLOG(GetPawn(), LogPawnAction, Log, TEXT("%s> pushing action %s")
		, *GetName(), *GetNameSafe(ActionCopy));
	++CurrentActionIndex;	
	check(ActionCopy);
	RecentActionCopy_DEPRECATED = ActionCopy;
	return PushChildAction(*ActionCopy);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
