// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCUIHelpers.h"

#include "RCVirtualProperty.h"

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"

#include "GraphEditorSettings.h"
#include "Controller/RCController.h"

FLinearColor UE::RCUIHelpers::GetFieldClassTypeColor(const FProperty* InProperty)
{
	if (ensure(InProperty))
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		FEdGraphPinType PinType;
		if (Schema->ConvertPropertyToPinType(InProperty, PinType))
		{
			return Schema->GetPinTypeColor(PinType);
		}
	}

	return FLinearColor::White;
}

FText UE::RCUIHelpers::GetFieldClassDisplayName(const FProperty* InProperty)
{
	if (ensure(InProperty))
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		FEdGraphPinType PinType;

		if (Schema->ConvertPropertyToPinType(InProperty, PinType))
		{
			if(UObject* SubCategoryObject = PinType.PinSubCategoryObject.Get())
			{
				return FText::FromName(*SubCategoryObject->GetName());
			}
			
			return FText::FromName(PinType.PinCategory);
		}
	}

	return FText::GetEmpty();
}