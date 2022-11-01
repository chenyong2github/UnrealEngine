// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayInteractionsTypes.h"
#include "SmartObjectTypes.h"
#include "GameplayInteractionListenSlotEventsTask.generated.h"

class USmartObjectSubsystem;

/**
 * Task to listen Smart Object slot events on a specified slot.
 * Any event sent to the specified Smart Object slot will be translated to a State Tree event.
 */

USTRUCT()
struct FGameplayInteractionListenSlotEventsTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input")
	FSmartObjectSlotHandle TargetSlot;

	FDelegateHandle OnEventHandle;
};

USTRUCT(meta = (DisplayName = "(Gameplay Interaction) Listen Slot Events"))
struct FGameplayInteractionListenSlotEventsTask : public FGameplayInteractionStateTreeTask
{
	GENERATED_BODY()

	FGameplayInteractionListenSlotEventsTask();
	
	using FInstanceDataType = FGameplayInteractionListenSlotEventsTaskInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	/** Handle to retrieve USmartObjectSubsystem. */
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
};
