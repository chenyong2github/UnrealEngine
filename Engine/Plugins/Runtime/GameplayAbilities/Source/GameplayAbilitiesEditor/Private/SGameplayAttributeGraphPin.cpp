// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SGameplayAttributeGraphPin.h"
#include "Widgets/SBoxPanel.h"
#include "AttributeSet.h"
#include "SGameplayAttributeWidget.h"

#define LOCTEXT_NAMESPACE "K2Node"

void SGameplayAttributeGraphPin::Construct( const FArguments& InArgs, UEdGraphPin* InGraphPinObj )
{
	SGraphPin::Construct( SGraphPin::FArguments(), InGraphPinObj );
	LastSelectedProperty = nullptr;

}

TSharedRef<SWidget>	SGameplayAttributeGraphPin::GetDefaultValueWidget()
{
	// Parse out current default value	
	FString DefaultString = GraphPinObj->GetDefaultAsString();
	FGameplayAttribute DefaultAttribute;

	UScriptStruct* PinLiteralStructType = FGameplayAttribute::StaticStruct();
	if (!DefaultString.IsEmpty())
	{
		PinLiteralStructType->ImportText(*DefaultString, &DefaultAttribute, nullptr, EPropertyPortFlags::PPF_SerializedAsImportText, GError, PinLiteralStructType->GetName(), true);
	}

	//Create widget
	return SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SGameplayAttributeWidget)
			.OnAttributeChanged(this, &SGameplayAttributeGraphPin::OnAttributeChanged)
			.DefaultProperty(DefaultAttribute.GetUProperty())
		];
}

void SGameplayAttributeGraphPin::OnAttributeChanged(FProperty* SelectedAttribute)
{
	FString FinalValue;
	FGameplayAttribute NewAttributeStruct;
	NewAttributeStruct.SetUProperty(SelectedAttribute);

	FGameplayAttribute::StaticStruct()->ExportText(FinalValue, &NewAttributeStruct, &NewAttributeStruct, nullptr, EPropertyPortFlags::PPF_SerializedAsImportText, nullptr);

	GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, FinalValue);
	LastSelectedProperty = SelectedAttribute;
}

#undef LOCTEXT_NAMESPACE
