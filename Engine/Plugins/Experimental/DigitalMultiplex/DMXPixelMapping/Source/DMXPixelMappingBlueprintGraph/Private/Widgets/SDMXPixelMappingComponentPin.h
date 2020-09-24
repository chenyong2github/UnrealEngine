// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"
#include "SNameComboBox.h"
#include "ScopedTransaction.h"

#include "DMXPixelMapping.h"

/** 
 * Cusotom widget for Pixel Mapping component pin.
 */
template<typename TComponentClass>
class SDMXPixelMappingComponentPin
	: public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingComponentPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj, UDMXPixelMapping* InDMXPixelMapping)
	{
		DMXPixelMappingWeakPtr = InDMXPixelMapping;

		InDMXPixelMapping->GetAllComponentsNamesOfClass<TComponentClass>(NameList);

		SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
	}

protected:

	/**
	 *	Function to create class specific widget.
	 *
	 *	@return Reference to the newly created widget object
	 */
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override
	{
		TSharedPtr<FName> CurrentlySelectedName;

		if (GraphPinObj)
		{
			// Preserve previous selection, or set to first in list
			FName PreviousSelection = FName(*GraphPinObj->GetDefaultAsString());
			for (TSharedPtr<FName> ListNamePtr : NameList)
			{
				if (PreviousSelection == *ListNamePtr.Get())
				{
					CurrentlySelectedName = ListNamePtr;
					break;
				}
			}

			// Reset to default name
			SetNameToPin(CurrentlySelectedName);
		}

		return SAssignNew(ComboBox, SNameComboBox)
				.ContentPadding(FMargin(6.0f, 2.0f))
				.OptionsSource(&NameList)
				.InitiallySelectedItem(CurrentlySelectedName)
				.OnSelectionChanged(this, &SDMXPixelMappingComponentPin::ComboBoxSelectionChanged)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility);
	}

	/**
	 *	Function to set the newly selected index
	 *
	 * @param NameItem The newly selected item in the combo box
	 * @param SelectInfo Provides context on how the selection changed
	 */
	void ComboBoxSelectionChanged(TSharedPtr<FName> NameItem, ESelectInfo::Type SelectInfo)
	{
		SetNameToPin(NameItem);
	}
private:
	
	/** Set name from Combo Box to input pin */
	void SetNameToPin(TSharedPtr<FName> NameItem)
	{
		FName Name = NameItem.IsValid() ? *NameItem : NAME_None;
		if (auto Schema = (GraphPinObj ? GraphPinObj->GetSchema() : NULL))
		{
			FString NameAsString = Name.ToString();
			if (GraphPinObj->GetDefaultAsString() != NameAsString)
			{
				const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeNameListPinValue", "Change Name List Pin Value"));
				GraphPinObj->Modify();

				Schema->TrySetDefaultValue(*GraphPinObj, NameAsString);
			}
		}
	}

private:
	/** Weak pointer to Pixel Mapping Object */
	TWeakObjectPtr<UDMXPixelMapping> DMXPixelMappingWeakPtr;

	/** Reference to Combo Box object */
	TSharedPtr<SNameComboBox> ComboBox;

	/** List of available component names */
	TArray<TSharedPtr<FName>> NameList;
};
