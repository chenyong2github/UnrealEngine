// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayInteractionsTypes.h"
#include "SmartObjectTypes.h"
#include "GameplayInteractionSendSlotEventTask.generated.h"

class USmartObjectSubsystem;

/**
 * Task to send event to a specified Smart Object Slot based on the tasks lifetime. 
 */

USTRUCT()
struct FGameplayInteractionSendSlotEventTaskInstanceData
{
	GENERATED_BODY()

	/** The slot to send the event to. */
	UPROPERTY(EditAnywhere, Category="Input")
	FSmartObjectSlotHandle TargetSlot;
};

USTRUCT(meta = (DisplayName = "(Gameplay Interaction) Send Slot Event"))
struct FGameplayInteractionSendSlotEventTask : public FGameplayInteractionStateTreeTask
{
	GENERATED_BODY()

	FGameplayInteractionSendSlotEventTask();
	
	using FInstanceDataType = FGameplayInteractionSendSlotEventTaskInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	/** Tag of the event to send. */
	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTag EventTag;

	/** Payload of the event to send. */
	UPROPERTY(EditAnywhere, Category = Parameter)
	FInstancedStruct Payload;

	/** Specifies under which conditions to send the event. */
	UPROPERTY(EditAnywhere, Category = Parameter)
	EGameplayInteractionTaskTrigger Trigger = EGameplayInteractionTaskTrigger::OnEnterState;

	/** Handle to retrieve USmartObjectSubsystem. */
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
};
