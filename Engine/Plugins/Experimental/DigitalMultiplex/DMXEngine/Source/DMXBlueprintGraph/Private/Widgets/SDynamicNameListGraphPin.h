// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"
#include "Widgets/SNameListPicker.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

template<typename TStructType>
class DMXBLUEPRINTGRAPH_API SDynamicNameListGraphPin
	: public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SDynamicNameListGraphPin)
	{}
		SLATE_ARGUMENT(FSimpleMulticastDelegate*, UpdateOptionsDelegate)
		SLATE_ATTRIBUTE(TArray<FName>, OptionsSource)
	SLATE_END_ARGS()

	/**  Slate widget construction method */
	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
	{
		OptionsSource = InArgs._OptionsSource;
		SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
	}

	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override
	{
		//Create widget
		return SNew(SNameListPicker)
			.HasMultipleValues(false)
			.Value(this, &SDynamicNameListGraphPin::GetValue)
			.OnValueChanged(this, &SDynamicNameListGraphPin::SetValue)
			.UpdateOptionsDelegate(UpdateOptionsDelegate)
			.OptionsSource(OptionsSource)
			.Visibility(this, &SGraphPin::GetDefaultValueVisibility);
	}
	//~ End SGraphPin Interface

private:
	FName GetValue() const
	{
		TStructType ProtocolName;

		if (!GraphPinObj->GetDefaultAsString().IsEmpty())
		{
			TStructType::StaticStruct()->ImportText(*GraphPinObj->GetDefaultAsString(), &ProtocolName, nullptr, EPropertyPortFlags::PPF_None, GLog, TStructType::StaticStruct()->GetName());
		}

		return ProtocolName;
	}

	void SetValue(FName NewValue)
	{
		FString ValueString;
		TStructType NewProtocolName(NewValue);
		TStructType::StaticStruct()->ExportText(ValueString, &NewProtocolName, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, ValueString);
	}

private:
	TAttribute<TArray<FName>> OptionsSource;
	FSimpleMulticastDelegate* UpdateOptionsDelegate;
};
