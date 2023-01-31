// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Input/VCamInputProcessor.h"

struct FVCamInputDeviceConfig;
class UVCamPlayerInput;

#include "VCamSubsystem.h"
#include "EnhancedInputSubsystemInterface.h"
#include "Tickable.h"
#include "InputVCamSubsystem.generated.h"

namespace UE::VCamCore::Private
{
	class FVCamInputProcessor;
}

class UVCamPlayerInput;

/**
 * Handles all input for UVCamComponent.
 * Essentially maps input devices to UVCamComponents, similar like APlayerController does for gameplay code.
 */
UCLASS()
class VCAMCORE_API UInputVCamSubsystem : public UVCamSubsystem, public IEnhancedInputSubsystemInterface, public FTickableGameObject
{
	GENERATED_BODY()
public:
	
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface
	
	/** Inputs a key on this subsystem's player input which can then be processed as normal during Tick. */
	bool InputKey(const FInputKeyParams& Params);

	/** Pushes this input component onto the stack to be processed by this subsystem's tick function */
	void PushInputComponent(UInputComponent* InInputComponent);
	/** Removes this input component onto the stack to be processed by this subsystem's tick function */
	bool PopInputComponent(UInputComponent* InInputComponent);
	
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetShouldConsumeGamepadInput(EVCamGamepadInputMode GamepadInputMode);

	const FVCamInputDeviceConfig& GetInputSettings() const;
	void SetInputSettings(const FVCamInputDeviceConfig& Input);
	
	//~ Begin FTickableGameObject interface
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual bool IsTickableInEditor() const { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsAllowedToTick() const override;
	virtual void Tick(float DeltaTime) override;
	TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UInputVCamSubsystem, STATGROUP_Tickables); }
	//~ End FTickableGameObject interface

	//~ Begin IEnhancedInputSubsystemInterface Interface
	virtual UEnhancedPlayerInput* GetPlayerInput() const override;
	//~ End IEnhancedInputSubsystemInterface Interface

private:

	UPROPERTY(Transient, Instanced)
	TObjectPtr<UVCamPlayerInput> PlayerInput;
	
	TSharedPtr<UE::VCamCore::Private::FVCamInputProcessor> InputPreprocessor;
	
	/** Internal. This is the current stack of InputComponents that is being processed by the PlayerInput. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UInputComponent>> CurrentInputStack;
};
