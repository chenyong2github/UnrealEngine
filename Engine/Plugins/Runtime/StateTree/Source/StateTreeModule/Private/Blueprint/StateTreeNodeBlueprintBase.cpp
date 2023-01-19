// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/StateTreeNodeBlueprintBase.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"
#include "VisualLogger/VisualLogger.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeNodeBlueprintBase)

UWorld* UStateTreeNodeBlueprintBase::GetWorld() const
{
	// The items are duplicated as the State Tree execution context as outer, so this should be essentially the same as GetWorld() on StateTree context.
	// The CDO is used by the BP editor to check for certain functionality, make it return nullptr so that the GetWorld() passes as overridden. 
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (CachedOwner != nullptr)
		{
			return CachedOwner->GetWorld();
		}
		if (UObject* Outer = GetOuter())
		{
			return Outer->GetWorld();
		}
	}
	
	return nullptr;
}

AActor* UStateTreeNodeBlueprintBase::GetOwnerActor(const FStateTreeExecutionContext& Context) const
{
	if (const AAIController* Controller = Cast<AAIController>(Context.GetOwner()))
	{
		return Controller->GetPawn();
	}
	
	return Cast<AActor>(Context.GetOwner());
}

void UStateTreeNodeBlueprintBase::SetCachedEventQueueFromContext(const FStateTreeExecutionContext& Context) const
{
	CachedEventQueue = &Context.GetMutableEventQueue();
	CachedOwner = Context.GetOwner();
}

void UStateTreeNodeBlueprintBase::ClearCachedEventQueue() const
{
	CachedEventQueue = nullptr;
	CachedOwner = nullptr;
}

void UStateTreeNodeBlueprintBase::SendEvent(const FStateTreeEvent& Event)
{
	if (CachedEventQueue == nullptr || CachedOwner == nullptr)
	{
		UE_VLOG_UELOG(this, LogStateTree, Error, TEXT("Trying to call SendEvent() while node is not active. Use SendEvent() on UStateTreeComponent instead for sending signals externally."));
		return;
	}
	CachedEventQueue->SendEvent(CachedOwner, Event.Tag, Event.Payload, Event.Origin);
}

