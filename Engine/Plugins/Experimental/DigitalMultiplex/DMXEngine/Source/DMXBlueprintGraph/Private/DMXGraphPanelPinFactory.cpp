// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXGraphPanelPinFactory.h"
#include "K2Node.h"
#include "EdGraphSchema_K2.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SGraphPin.h"
#include "SGraphPinNameList.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXProtocolTypes.h"
#include "Widgets/SDynamicNameListGraphPin.h"
#include "Widgets/SDMXEntityReferenceGraphPin.h"
#include "Widgets/SNullGraphPin.h"

TSharedPtr<class SGraphPin> FDMXGraphPanelPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* PinStructType = Cast<UScriptStruct>(InPin->PinType.PinSubCategoryObject.Get()))
		{
			if (PinStructType->IsChildOf(FDMXProtocolName::StaticStruct()))
			{
				return SNew(SDynamicNameListGraphPin<FDMXProtocolName>, InPin)
					.OptionsSource(MakeAttributeLambda(&FDMXProtocolName::GetPossibleValues));
			}
			else if (PinStructType->IsChildOf(FDMXFixtureCategory::StaticStruct()))
			{
				return SNew(SDynamicNameListGraphPin<FDMXFixtureCategory>, InPin)
					.OptionsSource(MakeAttributeLambda(&FDMXFixtureCategory::GetPossibleValues))
					.UpdateOptionsDelegate(&FDMXFixtureCategory::OnPossibleValuesUpdated);
			}
			else if (PinStructType->IsChildOf(FDMXEntityReference::StaticStruct()))
			{
				return SNew(SDMXEntityReferenceGraphPin, InPin);
			}
		}
	}
	else if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		if (UClass* PinObjectType = Cast<UClass>(InPin->PinType.PinSubCategoryObject.Get()))
		{
			if (PinObjectType->IsChildOf(UDMXEntity::StaticClass()))
			{
				return SNew(SNullGraphPin, InPin);
			}
		}
	}

	return FGraphPanelPinFactory::CreatePin(InPin);
}