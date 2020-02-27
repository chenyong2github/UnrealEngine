// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNameListPicker.h"

#include "DMXEditorLog.h"

#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "SDMXProtocolNamePicker"

void SNameListPicker::Construct(const FArguments& InArgs)
{
	ValueAttribute = InArgs._Value;
	OnValueChangedDelegate = InArgs._OnValueChanged;
	HasMultipleValuesAttribute = InArgs._HasMultipleValues;

	OptionsSourceAttr = InArgs._OptionsSource;
	UpdateOptionsSource();

	UpdateOptionsDelegate = InArgs._UpdateOptionsDelegate;
	if (UpdateOptionsDelegate)
	{
		UpdateOptionsHandle = UpdateOptionsDelegate->Add(FSimpleDelegate::CreateSP(this, &SNameListPicker::UpdateOptionsSource));
	}

	TSharedPtr<FName> InitiallySelected = nullptr;
	for (TSharedPtr<FName> NameItem : OptionsSource)
	{
		if (NameItem)
		{
			if (ValueAttribute.Get() == *NameItem)
			{
				InitiallySelected = NameItem;
				break;
			}
		}
	}

	ChildSlot
	[
		SAssignNew(PickerComboButton, SComboBox<TSharedPtr<FName>>)
		.OptionsSource(&OptionsSource)
		.OnGenerateWidget(this, &SNameListPicker::GenerateNameItemWidget)
		.OnSelectionChanged(this, &SNameListPicker::HandleSelectionChanged)
		.InitiallySelectedItem(InitiallySelected)
		[
			SNew(STextBlock)
			.Text(this, &SNameListPicker::GetCurrentNameLabel)
		]
	];
}

void SNameListPicker::UpdateOptionsSource()
{
	OptionsSource.Reset();
	OptionsSource.Reserve(OptionsSourceAttr.Get().Num());
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
		OnValueChangedDelegate.ExecuteIfBound(*Item);
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

FText SNameListPicker::GetCurrentNameLabel() const
{
	const bool bHasMultipleValues = HasMultipleValuesAttribute.Get();
	if (bHasMultipleValues)
	{
		return LOCTEXT("MultipleValuesText", "<multiple values>");
	}

	return FText::FromName(ValueAttribute.Get());
}

#undef LOCTEXT_NAMESPACE
