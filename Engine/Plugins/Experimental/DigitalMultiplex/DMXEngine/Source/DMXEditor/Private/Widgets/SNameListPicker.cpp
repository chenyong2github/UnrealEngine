// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNameListPicker.h"

#include "DMXEditorLog.h"
#include "DMXNameListItem.h"

#include "EditorStyleSet.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "SDMXProtocolNamePicker"

const FText SNameListPicker::NoneLabel = LOCTEXT("NoneLabel", "<Select a Value>");

void SNameListPicker::Construct(const FArguments& InArgs)
{
	ValueAttribute = InArgs._Value;
	OnValueChangedDelegate = InArgs._OnValueChanged;
	HasMultipleValuesAttribute = InArgs._HasMultipleValues;

	bCanBeNone = InArgs._bCanBeNone;
	bDisplayWarningIcon = InArgs._bDisplayWarningIcon;
	OptionsSourceAttr = InArgs._OptionsSource;
	UpdateOptionsSource();
	IsValidAttr = InArgs._IsValid;

	UpdateOptionsDelegate = InArgs._UpdateOptionsDelegate;
	if (UpdateOptionsDelegate)
	{
		UpdateOptionsHandle = UpdateOptionsDelegate->Add(FSimpleDelegate::CreateSP(this, &SNameListPicker::UpdateOptionsSource));
	}

	ChildSlot
	[
		SAssignNew(PickerComboButton, SComboBox<TSharedPtr<FName>>)
		.OptionsSource(&OptionsSource)
		.OnGenerateWidget(this, &SNameListPicker::GenerateNameItemWidget)
		.OnSelectionChanged(this, &SNameListPicker::HandleSelectionChanged)
		.OnComboBoxOpening(this, &SNameListPicker::UpdateSelectedOption)
		.InitiallySelectedItem(GetSelectedItemFromCurrentValue())
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.Warning"))
				.ToolTipText(LOCTEXT("WarningToolTip", "Value was removed. Please, select another one."))
				.Visibility(this, &SNameListPicker::GetWarningVisibility)
			]
		
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.Padding(0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SNameListPicker::GetCurrentNameLabel)
			]
		]
	];
}

void SNameListPicker::UpdateOptionsSource()
{
	// Number of options with or without the <None> option
	const int32 NumOptions = OptionsSourceAttr.Get().Num() + (bCanBeNone ? 1 : 0);

	OptionsSource.Reset();
	OptionsSource.Reserve(NumOptions);

	// If we can have <None>, it's the first option
	if (bCanBeNone)
	{
		OptionsSource.Add(MakeShared<FName>(FDMXNameListItem::None));
	}

	for (const FName& Name : OptionsSourceAttr.Get())
	{
		OptionsSource.Add(MakeShared<FName>(Name));
	}
}

SNameListPicker::~SNameListPicker()
{
	if (UpdateOptionsDelegate)
	{
		UpdateOptionsDelegate->Remove(UpdateOptionsHandle);
		UpdateOptionsDelegate = nullptr;
	}
}

TSharedRef<SWidget> SNameListPicker::GenerateNameItemWidget(TSharedPtr<FName> InItem)
{
	if (!InItem.IsValid())
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("InItem for GenerateProtocolItemWidget was null!"));
		return SNew(STextBlock)
			.Text(LOCTEXT("NullComboBoxItemLabel", "Null Error"));
	}

	if (InItem->IsEqual(FDMXNameListItem::None))
	{
		return SNew(STextBlock)
			.Text(NoneLabel);
	}

	return SNew(STextBlock)
		.Text(FText::FromName(*InItem));
}

void SNameListPicker::HandleSelectionChanged(const TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item.IsValid())
	{
		UE_LOG_DMXEDITOR(Error, TEXT("HandleProtocolChanged called with null Item pointer"));
		return;
	}

	if (OnValueChangedDelegate.IsBound())
	{
		OnValueChangedDelegate.Execute(*Item);
	}
	else if (!ValueAttribute.IsBound())
	{
		ValueAttribute = *Item;
	}

	TSharedPtr<SComboButton> PickerComboButtonPin = PickerComboButton.Pin();
	if (PickerComboButtonPin.IsValid())
	{
		PickerComboButtonPin->SetIsOpen(false);
	}
}

TSharedPtr<FName> SNameListPicker::GetSelectedItemFromCurrentValue() const
{
	TSharedPtr<FName> InitiallySelected = nullptr;

	const bool bHasMultipleValues = HasMultipleValuesAttribute.Get();
	if (bHasMultipleValues)
	{
		return InitiallySelected;
	}

	const FName CurrentValue = ValueAttribute.Get();

	for (TSharedPtr<FName> NameItem : OptionsSource)
	{
		if (NameItem != nullptr)
		{
			if (CurrentValue.IsEqual(*NameItem))
			{
				InitiallySelected = NameItem;
				break;
			}
		}
	}

	return InitiallySelected;
}

void SNameListPicker::UpdateSelectedOption()
{
	if (TSharedPtr<SComboBox<TSharedPtr<FName>>> PinnedButton = PickerComboButton.Pin())
	{
		PinnedButton->SetSelectedItem(GetSelectedItemFromCurrentValue());
	}
}

EVisibility SNameListPicker::GetWarningVisibility() const
{
	if (!bDisplayWarningIcon || HasMultipleValuesAttribute.Get())
	{
		return EVisibility::Collapsed;
	}

	if (!IsValidAttr.Get())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

FText SNameListPicker::GetCurrentNameLabel() const
{
	const bool bHasMultipleValues = HasMultipleValuesAttribute.Get();
	if (bHasMultipleValues)
	{
		return LOCTEXT("MultipleValuesText", "<multiple values>");
	}

	const FName CurrentName = ValueAttribute.Get();
	if (CurrentName.IsEqual(FDMXNameListItem::None))
	{
		return NoneLabel;
	}

	return FText::FromName(CurrentName);
}

#undef LOCTEXT_NAMESPACE
