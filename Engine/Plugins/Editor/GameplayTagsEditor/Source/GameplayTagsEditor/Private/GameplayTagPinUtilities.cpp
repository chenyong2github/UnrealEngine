// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplayTagPinUtilities.h"
#include "GameplayTagsManager.h"
#include "SGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableSet.h"

FString GameplayTagPinUtilities::ExtractTagFilterStringFromGraphPin(UEdGraphPin* InTagPin)
{
	FString FilterString;

	if (ensure(InTagPin))
	{
		const UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
		if (UScriptStruct* PinStructType = Cast<UScriptStruct>(InTagPin->PinType.PinSubCategoryObject.Get()))
		{
			FilterString = TagManager.GetCategoriesMetaFromField(PinStructType);
		}

		if (FilterString.IsEmpty())
		{
			if (UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(InTagPin->GetOwningNode()))
			{
				if (UFunction* ThisFunction = CallFuncNode->GetTargetFunction())
				{
					FilterString = TagManager.GetCategoriesMetaFromFunction(ThisFunction, InTagPin->PinName);
				}
			}
			else if (UK2Node_VariableSet* ThisVariable = Cast<UK2Node_VariableSet>(InTagPin->GetOwningNode()))
			{
				if (UProperty* Property = ThisVariable->GetPropertyForVariable())
				{
					FilterString = TagManager.GetCategoriesMetaFromField(Property);
				}
			}
		}
	}

	return FilterString;
}
