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

// Global input handling subsystem
// TODO: For now this is a non-functional placeholder.
UCLASS()
class ENHANCEDINPUT_API UEnhancedInputEngineSubsystem : public UEngineSubsystem, public IEnhancedInputSubsystemInterface
{
	GENERATED_BODY()

private:
	UPROPERTY()
	UEnhancedPlayerInput* PlayerInput;

public:

	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// End USubsystem

	// Begin IEnhancedInputSubsystemInterface
	virtual UEnhancedPlayerInput* GetPlayerInput() const override;
	// End IEnhancedInputSubsystemInterface

};
