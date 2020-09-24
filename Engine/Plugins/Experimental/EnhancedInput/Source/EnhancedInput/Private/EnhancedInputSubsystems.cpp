// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputSubsystems.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputModule.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectIterator.h"


// **************************************************************************************************
// *
// * UEnhancedInputLocalPlayerSubsystem
// *
// **************************************************************************************************

UEnhancedPlayerInput* UEnhancedInputLocalPlayerSubsystem::GetPlayerInput() const
{	
	if (APlayerController* PlayerController = GetLocalPlayer()->GetPlayerController(GetWorld()))
	{
		return Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput);
	}
	return nullptr;
}

// **************************************************************************************************
// *
// * UEnhancedInputEngineSubsystem
// *
// **************************************************************************************************

void UEnhancedInputEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{	
	PlayerInput = nullptr;
	//PlayerInput = NewObject<UEnhancedPlayerInput>();	// TODO: Remove UPlayerInput Within? Create an empty player controller?
}

UEnhancedPlayerInput* UEnhancedInputEngineSubsystem::GetPlayerInput() const
{
	return PlayerInput;
}
