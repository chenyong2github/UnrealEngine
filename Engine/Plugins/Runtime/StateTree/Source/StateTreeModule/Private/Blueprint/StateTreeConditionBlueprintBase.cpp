// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "CoreMinimal.h"
#include "StateTreeExecutionContext.h"
#include "Engine/Blueprint.h"

//----------------------------------------------------------------------//
//  UStateTreeConditionBlueprintBase
//----------------------------------------------------------------------//

bool UStateTreeConditionBlueprintBase::TestCondition(FStateTreeExecutionContext& Context) const
{
	FScopedCurrentContext(*this, Context);
	AActor* OwnerActor = GetOwnerActor(Context);
	return ReceiveTestCondition(OwnerActor);
}

//----------------------------------------------------------------------//
//  FStateTreeBlueprintConditionWrapper
//----------------------------------------------------------------------//

bool FStateTreeBlueprintConditionWrapper::Link(FStateTreeLinker& Linker)
{
	const UStateTreeConditionBlueprintBase* CondCDO =  ConditionClass ? ConditionClass->GetDefaultObject<UStateTreeConditionBlueprintBase>() : nullptr;
	if (CondCDO != nullptr)
	{
		CondCDO->LinkExternalData(Linker, ExternalDataHandles);
	}

	return true;
}

bool FStateTreeBlueprintConditionWrapper::TestCondition(FStateTreeExecutionContext& Context) const
{
	if (UStateTreeConditionBlueprintBase* Instance = Context.GetInstanceDataPtr<UStateTreeConditionBlueprintBase>(*this))
	{
		Instance->CopyExternalData(Context, ExternalDataHandles);
		return Instance->TestCondition(Context);
	}
	return false;
}

