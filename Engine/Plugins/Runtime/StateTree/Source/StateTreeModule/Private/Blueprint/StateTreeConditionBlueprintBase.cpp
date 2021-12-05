// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "CoreMinimal.h"
#include "StateTreeExecutionContext.h"
#include "Engine/Blueprint.h"

//----------------------------------------------------------------------//
//  UStateTreeConditionBlueprintBase
//----------------------------------------------------------------------//

bool UStateTreeConditionBlueprintBase::TestCondition(FStateTreeExecutionContext& Context)
{
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
	if (UStateTreeConditionBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeConditionBlueprintBase>(DataViewIndex))
	{
		Instance->CopyExternalData(Context, ExternalDataHandles);
		return Instance->TestCondition(Context);
	}
	return false;
}

#if WITH_EDITOR
FText FStateTreeBlueprintConditionWrapper::GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const
{
	const UClass* Class = Cast<const UClass>(InstanceData.GetStruct());

	if (Class != nullptr)
	{
		if (UBlueprint* ClassBP = Cast<UBlueprint>(Class->ClassGeneratedBy))
		{
			if (!ClassBP->BlueprintDescription.IsEmpty())
			{
				return FText::FromString(ClassBP->BlueprintDescription);
			}
		}

		return Class->GetDisplayNameText();
	}
	
	return FText::GetEmpty();
}
#endif
