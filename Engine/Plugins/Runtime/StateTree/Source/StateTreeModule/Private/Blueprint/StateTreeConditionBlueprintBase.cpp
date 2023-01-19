// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeNodeBlueprintBase.h"
#include "StateTreeExecutionContext.h"
#include "BlueprintNodeHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeConditionBlueprintBase)

//----------------------------------------------------------------------//
//  UStateTreeConditionBlueprintBase
//----------------------------------------------------------------------//

UStateTreeConditionBlueprintBase::UStateTreeConditionBlueprintBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bHasTestCondition = BlueprintNodeHelpers::HasBlueprintFunction(TEXT("ReceiveTestCondition"), *this, *StaticClass());
}

bool UStateTreeConditionBlueprintBase::TestCondition(FStateTreeExecutionContext& Context) const
{
	if (bHasTestCondition)
	{
		// Cache the owner and event queue for the duration the condition is evaluated.
		SetCachedEventQueueFromContext(Context);

		const bool bResult = ReceiveTestCondition();

		ClearCachedEventQueue();

		return bResult;
	}
	return false;
}

//----------------------------------------------------------------------//
//  FStateTreeBlueprintConditionWrapper
//----------------------------------------------------------------------//

bool FStateTreeBlueprintConditionWrapper::TestCondition(FStateTreeExecutionContext& Context) const
{
	UStateTreeConditionBlueprintBase* Condition = Context.GetInstanceDataPtr<UStateTreeConditionBlueprintBase>(*this);
	check(Condition);
	return Condition->TestCondition(Context);
}


