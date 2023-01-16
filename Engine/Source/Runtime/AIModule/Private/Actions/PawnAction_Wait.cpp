// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actions/PawnAction_Wait.h"
#include "TimerManager.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PawnAction_Wait)

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UDEPRECATED_UPawnAction_Wait::UDEPRECATED_UPawnAction_Wait(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TimeToWait(0.f)
	, FinishTimeStamp(0.f)
{

}

UDEPRECATED_UPawnAction_Wait* UDEPRECATED_UPawnAction_Wait::CreateAction(UWorld& World, float InTimeToWait)
{
	UDEPRECATED_UPawnAction_Wait* Action = UDEPRECATED_UPawnAction::CreateActionInstance<UDEPRECATED_UPawnAction_Wait>(World);
	
	if (Action != NULL)
	{
		Action->TimeToWait = InTimeToWait;
	}

	return Action;
}

bool UDEPRECATED_UPawnAction_Wait::Start()
{
	if (Super::Start())
	{
		if (TimeToWait >= 0)
		{
			GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UDEPRECATED_UPawnAction_Wait::TimerDone, TimeToWait);
		}
		// else hang in there for ever!

		return true;
	}

	return false;
}

bool UDEPRECATED_UPawnAction_Wait::Pause(const UDEPRECATED_UPawnAction* PausedBy)
{
	GetWorld()->GetTimerManager().PauseTimer(TimerHandle);
	return true;
}

bool UDEPRECATED_UPawnAction_Wait::Resume()
{
	GetWorld()->GetTimerManager().UnPauseTimer(TimerHandle);
	return true;
}

EPawnActionAbortState::Type UDEPRECATED_UPawnAction_Wait::PerformAbort(EAIForceParam::Type ShouldForce)
{
	GetWorld()->GetTimerManager().ClearTimer(TimerHandle);
	return EPawnActionAbortState::AbortDone;
}

void UDEPRECATED_UPawnAction_Wait::TimerDone()
{
	Finish(EPawnActionResult::Success);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
