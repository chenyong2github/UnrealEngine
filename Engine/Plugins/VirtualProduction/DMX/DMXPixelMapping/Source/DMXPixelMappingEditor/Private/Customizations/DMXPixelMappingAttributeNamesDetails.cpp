// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingAttributeNamesDetails.h"

#include "DMXAttribute.h"
#include "Library/DMXEntityFixturePatch.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Algo/Find.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXPixelMappingAttributeNamesDetails"

class SDMXPixelMappingAttributeNamesComboBox
	: public SCompoundWidget
{
	DECLARE_DELEGATE_OneParam(FOnAttributeSelectionChanged, const FName& /** AttributeName */)

public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingAttributeNamesComboBox)
	{}

		/** Event raised when the selection changed */
		SLATE_EVENT(FOnAttributeSelectionChanged, OnSelectionChanged)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, const TArray<FName>& InOptions)
	{
		for (const FName& AttributeName : InOptions)
		{
			Options.Add(MakeShared<FName>(AttributeName));
		}

		OnSelectionChanged = InArgs._OnSelectionChanged;

		// Always have at least one option to display
		if (Options.IsEmpty())
		{
			Options.Add(MakeShared<FName>("No attributes available"));
		}

		ChildSlot
			[
				SAssignNew(ComboBox, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&Options)
				.OnSelectionChanged(this, &SDMXPixelMappingAttributeNamesComboBox::HandleSelectionChanged)
				.OnGenerateWidget(this, &SDMXPixelMappingAttributeNamesComboBox::OnGenerateComboBoxEntry)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
						{
							if (bHasMultipleValues)
							{
								return LOCTEXT("MultipleValuesText", "Multiple Values");
							}
							else
							{
								check(!Options.IsEmpty());

								const TSharedPtr<FName> SelectedItem = ComboBox->GetSelectedItem();
								return SelectedItem.IsValid() ? FText::FromString(SelectedItem->ToString()) : FText::FromString(TEXT("Invalid selection"));
							}
						})
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]
			];
	}

	/** Sets the selection */
	void SetSelection(const FName& Selection)
	{
		bHasMultipleValues = false;
		const TSharedPtr<FName>* SelectionPtr = Algo::FindByPredicate(Options, [&Selection](const TSharedPtr<FName>& AttributeName)
			{
				return *AttributeName == Selection;
			});

		ensureMsgf(SelectionPtr, TEXT("Cannot set selection of SDMXPixelMappingAttributeNamesComboBox. Selection %s is not provided as an option."), *Selection.ToString());
		if (SelectionPtr)
		{
			ComboBox->SetSelectedItem(*SelectionPtr);
		}
	}

	/** Sets the combo box to display 'Multiple Values'. Note, this is reset when the combo box selects a value. */
	void SetHasMultipleValues() { bHasMultipleValues = true; }

private:
	/*
	 * Make a widget for the given option
	 *
	 * @param InOption   An option from which to construct the widget
	 */
	TSharedRef<SWidget> OnGenerateComboBoxEntry(TSharedPtr<FName> AttributeName) const
	{
		return SNew(STextBlock)
			.Text(FText::FromString(AttributeName->ToString()))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
	}

	/*
	 * Handle current item selection change
	 *
	 * @param NewValue   A value to set as current item
	 */
	void HandleSelectionChanged(TSharedPtr<FName> NewValue, ESelectInfo::Type)
	{
		bHasMultipleValues = false;

		OnSelectionChanged.ExecuteIfBound(*NewValue.Get());
	}

private:
	/** The actual combo box */
	TSharedPtr<SComboBox<TSharedPtr<FName>>> ComboBox;

	/** Options source for the combo box */
	TArray<TSharedPtr<FName>> Options;

	/** If true, shows 'Multiple Values' instead of the selection */
	bool bHasMultipleValues = false;

	// Slate Args
	FOnAttributeSelectionChanged OnSelectionChanged;
};

TSharedRef<IPropertyTypeCustomization> FDMXPixelMappingAttributeNamesDetails::MakeInstance(const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> FixturePatches)
{
	return MakeShared<FDMXPixelMappingAttributeNamesDetails>(FixturePatches);
}

void FDMXPixelMappingAttributeNamesDetails::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FixtureGroupItemComponentsAttributes = GetFixtureGroupItemsAttributes(FixturePatches);

	const TSharedRef<SDMXPixelMappingAttributeNamesComboBox> AttributeNamesComboBox =
		SNew(SDMXPixelMappingAttributeNamesComboBox, FixtureGroupItemComponentsAttributes)
		.OnSelectionChanged_Lambda([this, PropertyHandle](const FName& NewValue)
			{
				SetAttributeValue(PropertyHandle, NewValue);
			});

	if (HasMultipleAttributeValues(PropertyHandle))
	{
		AttributeNamesComboBox->SetHasMultipleValues();
	}
	else if (FixtureGroupItemComponentsAttributes.Num() > 0)
	{
		FName InitialSelection = GetAttributeValue(PropertyHandle);

		// Set a valid attribute, or name none if none is available
		if (!FixtureGroupItemComponentsAttributes.Contains(InitialSelection))
		{
			if (FixtureGroupItemComponentsAttributes.IsEmpty())
			{
				SetAttributeValue(PropertyHandle, NAME_None);
			}
			else
			{
				SetAttributeValue(PropertyHandle, FixtureGroupItemComponentsAttributes[0]);
				InitialSelection = FixtureGroupItemComponentsAttributes[0];
			}
		}
		AttributeNamesComboBox->SetSelection(InitialSelection);
	}

	HeaderRow
		.NameContent()
		[
			SNew(STextBlock)
			.Text(PropertyHandle->GetPropertyDisplayName())
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		]
		.ValueContent()
		[
			AttributeNamesComboBox
		];
}

void FDMXPixelMappingAttributeNamesDetails::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	
}

bool FDMXPixelMappingAttributeNamesDetails::HasMultipleAttributeValues(TSharedRef<IPropertyHandle> AttributeHandle) const
{
	TArray<const void*> RawData;
	AttributeHandle->AccessRawData(RawData);

	TSet<FName> AttributeNames;
	for (const void* RawPtr : RawData)
	{
		if (RawPtr)
		{
			AttributeNames.Add(reinterpret_cast<const FDMXAttributeName*>(RawPtr)->Name);
		}
	}

	return AttributeNames.Num() > 1;
}

FName FDMXPixelMappingAttributeNamesDetails::GetAttributeValue(TSharedRef<IPropertyHandle> AttributeHandle) const
{
	if (!ensureMsgf(!HasMultipleAttributeValues(AttributeHandle), TEXT("Cannot get attribute value from handle when handle has mutliple values")))
	{
		return NAME_None;
	}

	TArray<const void*> RawData;
	AttributeHandle->AccessRawData(RawData);

	for (const void* RawPtr : RawData)
	{
		if (RawPtr)
		{
			return reinterpret_cast<const FDMXAttributeName*>(RawPtr)->Name;
		}
	}

	return NAME_None;
}

void FDMXPixelMappingAttributeNamesDetails::SetAttributeValue(TSharedRef<IPropertyHandle> AttributeHandle, const FName& NewValue)
{
	FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(AttributeHandle->GetProperty());

	TArray<void*> RawData;
	AttributeHandle->AccessRawData(RawData);

	for (void* SingleRawData : RawData)
	{
		FDMXAttributeName* PreviousValue = reinterpret_cast<FDMXAttributeName*>(SingleRawData);
		FDMXAttributeName NewAttributeName;
		NewAttributeName.SetFromName(NewValue);

		// Export new value to text format that can be imported later
		FString TextValue;
		StructProperty->Struct->ExportText(TextValue, &NewAttributeName, PreviousValue, nullptr, EPropertyPortFlags::PPF_None, nullptr);

		// Set values on edited property handle from exported text
		ensure(AttributeHandle->SetValueFromFormattedString(TextValue, EPropertyValueSetFlags::DefaultFlags) == FPropertyAccess::Result::Success);
	}
}

TArray<FName> FDMXPixelMappingAttributeNamesDetails::GetFixtureGroupItemsAttributes(const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& Patches)
{
	TArray<FName> ComponentsAttributes;

	if (!Patches.IsEmpty())
	{
		// Gather attribute names present in Patches
		for (TWeakObjectPtr<UDMXEntityFixturePatch> Patch : Patches)
		{
			if (!Patch.IsValid())
			{
				continue;
			}

			const TArray<FDMXAttributeName> AttributeNames = Patch->GetAllAttributesInActiveMode();
			for (FDMXAttributeName AttributeName : AttributeNames)
			{
				FixtureGroupItemComponentsAttributes.AddUnique(AttributeName.Name);
			}
		}

		for (TWeakObjectPtr<UDMXEntityFixturePatch> Patch : Patches)
		{
			FixtureGroupItemComponentsAttributes.RemoveAll([Patch](const FName& AttributeName)
			{
				if (!Patch.IsValid())
				{
					return true;
				}
				const TArray<FDMXAttributeName> AttributeNamesOfPatch = Patch->GetAllAttributesInActiveMode();
				return !AttributeNamesOfPatch.Contains(AttributeName);
			});
		}
	}

	return FixtureGroupItemComponentsAttributes;
}

#undef LOCTEXT_NAMESPACE
