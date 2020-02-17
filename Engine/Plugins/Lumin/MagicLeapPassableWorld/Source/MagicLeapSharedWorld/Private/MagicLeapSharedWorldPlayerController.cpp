// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapSharedWorldPlayerController.h"
#include "MagicLeapSharedWorldGameMode.h"
#include "MagicLeapSharedWorldGameState.h"
#include "GameFramework/GameState.h"
#include "Engine/World.h"

AMagicLeapSharedWorldPlayerController::AMagicLeapSharedWorldPlayerController(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bHasNewLocalWorldData(false)
, bIsChosenOne(false)
, bCanSendLocalData(false)
{}

void AMagicLeapSharedWorldPlayerController::ServerSetLocalWorldData_Implementation(const FMagicLeapSharedWorldLocalData& LocalWorldReplicationData)
{
	LocalWorldData = LocalWorldReplicationData;
	bHasNewLocalWorldData = true;

	UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("Received %d pin ids from client %s."), LocalWorldData.LocalPins.Num(), *GetName());
	for (const FMagicLeapSharedWorldPinData& PinData : LocalWorldData.LocalPins)
	{
		UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("Received pin id %s from client with state %s."), *PinData.PinID.ToString(), *PinData.PinState.ToString());
	}
}

void AMagicLeapSharedWorldPlayerController::ClientMarkReadyForSendingLocalData_Implementation()
{
	bCanSendLocalData = true;
}

void AMagicLeapSharedWorldPlayerController::ClientSetChosenOne_Implementation(bool bChosenOne)
{
	UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("Selected PC (%s) as chosen one"), *GetName());
	bIsChosenOne = bChosenOne;
}

bool AMagicLeapSharedWorldPlayerController::IsChosenOne() const
{
	return bIsChosenOne;
}

bool AMagicLeapSharedWorldPlayerController::CanSendLocalDataToServer() const
{
	return bCanSendLocalData;
}

void AMagicLeapSharedWorldPlayerController::ServerSetAlignmentTransforms_Implementation(const FMagicLeapSharedWorldAlignmentTransforms& InAlignmentTransforms)
{
	if (IsGameStateReady())
	{
		GetGameState()->AlignmentTransforms = InAlignmentTransforms;
	}
	else
	{
		UE_LOG(LogMagicLeapSharedWorld, Error, TEXT("Game state was not ready when ServerSetAlignmentTransforms was called!!!!!"));
	}
}

AMagicLeapSharedWorldGameState* AMagicLeapSharedWorldPlayerController::GetGameState()
{
	UWorld* World = GetWorld();
	return World != nullptr ? CastChecked<AMagicLeapSharedWorldGameState>(World->GetGameState()) : nullptr;
}

bool AMagicLeapSharedWorldPlayerController::IsGameStateReady()
{
	UWorld* World = GetWorld();
	AMagicLeapSharedWorldGameState* GameState = World != nullptr ? CastChecked<AMagicLeapSharedWorldGameState>(World->GetGameState()) : nullptr;
	return GameState != nullptr;
}
