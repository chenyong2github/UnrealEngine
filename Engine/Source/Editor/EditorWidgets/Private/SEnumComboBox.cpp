// Copyright Epic Games, Inc. All Rights Reserved.

#include "SEnumCombobox.h"
#include "EditorStyleSet.h"
#include "Types/SlateEnums.h"
#include "Widgets/SToolTip.h"

void SEnumComboBox::Construct(const FArguments& InArgs, const UEnum* InEnum)
{
	Enum = InEnum;
	CurrentValue = InArgs._CurrentValue;
	check(CurrentValue.IsBound());
	OnEnumSelectionChangedDelegate = InArgs._OnEnumSelectionChanged;
	OnGetToolTipForValue = InArgs._OnGetToolTipForValue;
	Font = InArgs._Font;
	bUpdatingSelectionInternally = false;

	for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
	{
		if (Enum->HasMetaData(TEXT("Hidden"), i) == false)
		{
			VisibleEnumNameIndices.Add(MakeShareable(new int32(i)));
		}
	}

	SComboBox::Construct(SComboBox<TSharedPtr<int32>>::FArguments()
		.ButtonStyle(InArgs._ButtonStyle)
		.OptionsSource(&VisibleEnumNameIndices)
		.OnGenerateWidget(this, &SEnumComboBox::OnGenerateWidget)
		.OnSelectionChanged(this, &SEnumComboBox::OnComboSelectionChanged)
		.OnComboBoxOpening(this, &SEnumComboBox::OnComboMenuOpening)
		.ContentPadding(InArgs._ContentPadding)
		[
			SNew(STextBlock)
			.Font(Font)
			.Text(this, &SEnumComboBox::GetCurrentValueText)
			.ToolTipText(this, &SEnumComboBox::GetCurrentValueTooltip)
		]);
}


FText SEnumComboBox::GetCurrentValueText() const
{
	int32 ValueNameIndex = Enum->GetIndexByValue(CurrentValue.Get());
	return Enum->GetDisplayNameTextByIndex(ValueNameIndex);
}

FText SEnumComboBox::GetCurrentValueTooltip() const
{
	int32 ValueNameIndex = Enum->GetIndexByValue(CurrentValue.Get());
	if (OnGetToolTipForValue.IsBound())
	{
		return OnGetToolTipForValue.Execute(ValueNameIndex);
	}
	return Enum->GetToolTipTextByIndex(ValueNameIndex);
}

TSharedRef<SWidget> SEnumComboBox::OnGenerateWidget(TSharedPtr<int32> InItem)
{
	return SNew(STextBlock)
		.Font(Font)
		.Text(Enum->GetDisplayNameTextByIndex(*InItem))
		.ToolTipText(Enum->GetToolTipTextByIndex(*InItem));
}

void SEnumComboBox::OnComboSelectionChanged(TSharedPtr<int32> InSelectedItem, ESelectInfo::Type SelectInfo)
{
	if (bUpdatingSelectionInternally == false)
	{
		OnEnumSelectionChangedDelegate.ExecuteIfBound(*InSelectedItem, SelectInfo);
	}
}

void SEnumComboBox::OnComboMenuOpening()
{
	int32 CurrentNameIndex = Enum->GetIndexByValue(CurrentValue.Get());
	TSharedPtr<int32> FoundNameIndexItem;
	for (int32 i = 0; i < VisibleEnumNameIndices.Num(); i++)
	{
		if (*VisibleEnumNameIndices[i] == CurrentNameIndex)
		{
			FoundNameIndexItem = VisibleEnumNameIndices[i];
			break;
		}
	}
	if (FoundNameIndexItem.IsValid())
	{
		bUpdatingSelectionInternally = true;
		SetSelectedItem(FoundNameIndexItem);
		bUpdatingSelectionInternally = false;
	}
}
