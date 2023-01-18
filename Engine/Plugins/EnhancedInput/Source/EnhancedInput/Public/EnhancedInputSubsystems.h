// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EnhancedInputSubsystemInterface.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/WorldSubsystem.h"

#include "EnhancedInputSubsystems.generated.h"

class FEnhancedInputWorldProcessor;
enum class ETickableTickType : uint8;
struct FInputKeyParams;

DECLARE_LOG_CATEGORY_EXTERN(LogWorldSubsystemInput, Log, All);

// Per local player input subsystem
UCLASS()
class ENHANCEDINPUT_API UEnhancedInputLocalPlayerSubsystem : public ULocalPlayerSubsystem, public IEnhancedInputSubsystemInterface
{
	GENERATED_BODY()

public:

	// Begin IEnhancedInputSubsystemInterface
	virtual UEnhancedPlayerInput* GetPlayerInput() const override;
	virtual void ControlMappingsRebuiltThisFrame() override;
	// End IEnhancedInputSubsystemInterface

	/** A delegate that will be called when control mappings have been rebuilt this frame. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnControlMappingsRebuilt);

	/**
	 * Blueprint Event that is called at the end of any frame that Control Mappings have been rebuilt.
	 */
	UPROPERTY(BlueprintAssignable, DisplayName=OnControlMappingsRebuilt, Category = "Input")
	FOnControlMappingsRebuilt ControlMappingsRebuiltDelegate;
};

/**
 * Per world input subsystem that allows you to bind input delegates to actors without an owning Player Controller. 
 * This should be used when an actor needs to receive input delegates but will never have an owning Player Controller.
 * For example, you can add input delegates to unlock a door when the user has a certain set of keys pressed.
 * Be sure to enable input on the actor, or else the input delegates won't fire!
 * 
 * Note: if you do have an actor with an owning Player Controller use the local player input subsystem instead.
 */
UCLASS()
class ENHANCEDINPUT_API UEnhancedInputWorldSubsystem : public UTickableWorldSubsystem, public IEnhancedInputSubsystemInterface
{
	GENERATED_BODY()

public:

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem interface

	//~ Begin FTickableGameObject interface
	//virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual bool IsTickableInEditor() const { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual void Tick(float DeltaTime) override;
	TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEnhancedInputWorldSubsystem, STATGROUP_Tickables); }
	//~ End FTickableGameObject interface
	
	//~ Begin IEnhancedInputSubsystemInterface
	virtual UEnhancedPlayerInput* GetPlayerInput() const override;
	//~ End IEnhancedInputSubsystemInterface

	/** Adds this Actor's input component onto the stack to be processed by this subsystem's tick function */
	UFUNCTION(BlueprintCallable, Category = "Input|World")
	void AddActorInputComponent(AActor* Actor);

	/** Removes this Actor's input component from the stack to be processed by this subsystem's tick function */
	UFUNCTION(BlueprintCallable, Category = "Input|World")
	bool RemoveActorInputComponent(AActor* Actor);
	
	/** Start the consumption of input messages in this subsystem. This is required to have any Input Action delegates be fired. */
	UFUNCTION(BlueprintCallable, Category = "Input|World")
	void StartConsumingInput();

	/** Tells this subsystem to stop ticking and consuming any input. This will stop any Input Action Delegates from being called. */
	UFUNCTION(BlueprintCallable, Category = "Input|World")
	void StopConsumingInput();
	
	/** Returns true if this subsystem is currently consuming input */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Input|World")
    bool IsConsumingInput() const { return bIsCurrentlyConsumingInput; }
	
    /** Inputs a key on this subsystem's player input which can then be processed as normal during Tick. */
    bool InputKey(const FInputKeyParams& Params);
	
	/** Adds all the default mapping contexts */
	void AddDefaultMappingContexts();

	/** Removes all the default mapping contexts */
	void RemoveDefaultMappingContexts();
	
	virtual void ShowDebugInfo(UCanvas* Canvas) override;
private:

	/** The player input that is processing the input within this subsystem */
	UPROPERTY()
	TObjectPtr<UEnhancedPlayerInput> PlayerInput = nullptr;

	/**
	 * Input processor that is created on Initalize.
	 */
	TSharedPtr<FEnhancedInputWorldProcessor> InputPreprocessor = nullptr;
	
	/** If true, then this subsystem will Tick and process input delegates. */
	bool bIsCurrentlyConsumingInput = false;
	
	/** Internal. This is the current stack of InputComponents that is being processed by the PlayerInput. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UInputComponent>> CurrentInputStack;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "EnhancedInputWorldProcessor.h"
#include "Subsystems/EngineSubsystem.h"
#endif
