// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"

void AEnhancedInputPlayerController::InitInputSystem()
{
	if (PlayerInput == nullptr)
	{
		PlayerInput = NewObject<UEnhancedPlayerInput>(this);
	}

	Super::InitInputSystem();
}

void AEnhancedInputPlayerController::SetupInputComponent()
{
	if (InputComponent == NULL)
	{
		InputComponent = NewObject<UEnhancedInputComponent>(this, TEXT("PC_InputComponent0"));
		InputComponent->RegisterComponent();
	}

	Super::SetupInputComponent();
}