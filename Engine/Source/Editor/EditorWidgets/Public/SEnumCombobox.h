// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "Types/SlateEnums.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SToolTip.h"

/*
* Editor only widget for creating a combo-box which allows the user to pick the values from a dropdown combo-box. 
*/
class SEnumComboBox : public SComboBox<TSharedPtr<int32>>
 {
public:
	DECLARE_DELEGATE_TwoParams(FOnEnumSelectionChanged, int32 /*Selection*/, ESelectInfo::Type /*SelectionType*/);
public:
	SLATE_BEGIN_ARGS(SEnumComboBox)
		: _CurrentValue()
		, _ContentPadding(FMargin(4.0, 2.0))
		, _OnEnumSelectionChanged()
		, _ButtonStyle(nullptr)
	{}

		SLATE_ATTRIBUTE(int32, CurrentValue)
		SLATE_ATTRIBUTE(FMargin, ContentPadding)
		SLATE_ATTRIBUTE(FSlateFontInfo, Font)
		SLATE_ARGUMENT(FOnEnumSelectionChanged, OnEnumSelectionChanged)
		SLATE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UEnum* InEnum)
	{
		Enum = InEnum;
		CurrentValue = InArgs._CurrentValue;
		check(CurrentValue.IsBound());
		OnEnumSelectionChangedDelegate = InArgs._OnEnumSelectionChanged;
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
			.OnGenerateWidget_Lambda([this](TSharedPtr<int32> InItem)
			{
				return SNew(STextBlock)
					.Font(Font)
					.Text(Enum->GetDisplayNameTextByIndex(*InItem))
					.ToolTipText(Enum->GetToolTipTextByIndex(*InItem));
			})
			.OnSelectionChanged(this, &SEnumComboBox::OnComboSelectionChanged)
			.OnComboBoxOpening(this, &SEnumComboBox::OnComboMenuOpening)
			.ContentPadding(InArgs._ContentPadding)
			[
				SNew(STextBlock)
					.Font(Font)
					.Text(this, &SEnumComboBox::GetCurrentValue)
					.ToolTipText(this, &SEnumComboBox::GetCurrentValueTooltip)
			]);
	}

private:
	FText GetCurrentValue() const
	{
		int32 CurrentNameIndex = Enum->GetIndexByValue(CurrentValue.Get());
		return Enum->GetDisplayNameTextByIndex(CurrentNameIndex);
	}

	FText GetCurrentValueTooltip() const
	{
		int32 CurrentNameIndex = Enum->GetIndexByValue(CurrentValue.Get());
		return Enum->GetToolTipTextByIndex(CurrentNameIndex);
	}

	TSharedRef<SWidget> OnGenerateWidget(TSharedPtr<int32> InItem)
	{
		return SNew(STextBlock)
			.Text(Enum->GetDisplayNameTextByIndex(*InItem))
			.Font(Font);
	}

	void OnComboSelectionChanged(TSharedPtr<int32> InSelectedItem, ESelectInfo::Type SelectInfo)
	{
		if (bUpdatingSelectionInternally == false)
		{
			OnEnumSelectionChangedDelegate.ExecuteIfBound(*InSelectedItem, SelectInfo);
		}
	}

	void OnComboMenuOpening()
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

private:
	const UEnum* Enum;

	TAttribute<int32> CurrentValue;

	TAttribute< FSlateFontInfo > Font;

	TArray<TSharedPtr<int32>> VisibleEnumNameIndices;

	bool bUpdatingSelectionInternally;

	FOnEnumSelectionChanged OnEnumSelectionChangedDelegate;
};