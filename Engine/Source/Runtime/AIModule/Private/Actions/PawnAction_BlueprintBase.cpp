// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actions/PawnAction_BlueprintBase.h"
#include "BlueprintNodeHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PawnAction_BlueprintBase)

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UDEPRECATED_UPawnAction_BlueprintBase::UDEPRECATED_UPawnAction_BlueprintBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UClass* StopAtClass = UDEPRECATED_UPawnAction_BlueprintBase::StaticClass();
	bWantsTick = BlueprintNodeHelpers::HasBlueprintFunction(TEXT("ActionTick"), *this, *StopAtClass);
}

void UDEPRECATED_UPawnAction_BlueprintBase::Tick(float DeltaTime)
{
	// no need to call super implementation
	ActionTick(GetPawn(), DeltaTime);
}

bool UDEPRECATED_UPawnAction_BlueprintBase::Start()
{
	const bool bHasBeenEverStarted = HasBeenStarted();
	const bool bSuperResult = Super::Start();

	if (bHasBeenEverStarted == false && bSuperResult == true)
	{
		ActionStart(GetPawn());
	}

	return bSuperResult;
}

bool UDEPRECATED_UPawnAction_BlueprintBase::Pause(const UDEPRECATED_UPawnAction* PausedBy)
{
	const bool bResult = Super::Pause(PausedBy);
	if (bResult)
	{
		ActionPause(GetPawn());
	}
	return bResult;
}

bool UDEPRECATED_UPawnAction_BlueprintBase::Resume()
{
	const bool bResult = Super::Resume();
	if (bResult)
	{
		ActionResume(GetPawn());
	}

	return bResult;
}

void UDEPRECATED_UPawnAction_BlueprintBase::OnFinished(EPawnActionResult::Type WithResult)
{
	ActionFinished(GetPawn(), WithResult);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
