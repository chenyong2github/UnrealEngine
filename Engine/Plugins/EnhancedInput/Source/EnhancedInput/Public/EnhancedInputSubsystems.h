// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputSubsystemInterface.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/EngineSubsystem.h"

#include "EnhancedInputSubsystems.generated.h"

// Per local player input subsystem
UCLASS()
class ENHANCEDINPUT_API UEnhancedInputLocalPlayerSubsystem : public ULocalPlayerSubsystem, public IEnhancedInputSubsystemInterface
{
	GENERATED_BODY()

public:

	// Begin IEnhancedInputSubsystemInterface
	virtual UEnhancedPlayerInput* GetPlayerInput() const override;
	// End IEnhancedInputSubsystemInterface
};
