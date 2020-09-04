// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"
#include "Widgets/SNameListPicker.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DMXNameListItem.h"

template<typename TStructType>
class DMXBLUEPRINTGRAPH_API SDynamicNameListGraphPin
	: public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SDynamicNameListGraphPin)
	{}
	SLATE_END_ARGS()

	/**  Slate widget construction method */
	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
	{
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
			.UpdateOptionsDelegate(&TStructType::OnValuesChanged)
			.OptionsSource(MakeAttributeLambda(&TStructType::GetPossibleValues))
			.IsValid(this, &SDynamicNameListGraphPin::IsValueValid)
			.bCanBeNone(TStructType::bCanBeNone)
			.bDisplayWarningIcon(true)
			.Visibility(this, &SGraphPin::GetDefaultValueVisibility);
	}
	//~ End SGraphPin Interface

private:
	FName GetValue() const
	{
		TStructType NameItem;

		if (!GraphPinObj->GetDefaultAsString().IsEmpty())
		{
			TStructType::StaticStruct()->ImportText(*GraphPinObj->GetDefaultAsString(), &NameItem, nullptr, EPropertyPortFlags::PPF_None, GLog, TStructType::StaticStruct()->GetName());
		}

		return NameItem.GetName();
	}

	void SetValue(FName NewValue)
	{
		FString ValueString;
		TStructType NewNameItem(NewValue);
		TStructType::StaticStruct()->ExportText(ValueString, &NewNameItem, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, ValueString);
	}

	bool IsValueValid() const
	{
		return TStructType::IsValid(GetValue());
	}
};
