// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "SlateOptMacros.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "TimedDataMonitorEditorStyle.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"



template<class NumericType>
class STimedDataNumericEntryBox : public SCompoundWidget
{
private:
	using Super = SCompoundWidget;
	using FOnValueCommitted = typename SNumericEntryBox<NumericType>::FOnValueCommitted;
	
public:
	SLATE_BEGIN_ARGS(STimedDataNumericEntryBox)
			: _Value(0)
			, _MinValue(0)
			, _CanEdit(true)
			, _Amount(0)
			, _ShowAmount(false)
		{ }
		SLATE_ATTRIBUTE(NumericType, Value)
		SLATE_ARGUMENT(NumericType, MinValue)
		SLATE_ARGUMENT(FText, EditLabel)
		SLATE_ARGUMENT(bool, CanEdit);
		SLATE_ATTRIBUTE(NumericType, Amount);
		SLATE_ARGUMENT(bool, ShowAmount);
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_EVENT(FOnValueCommitted, OnValueCommitted)
	SLATE_END_ARGS()
	
public:
	void Construct(const FArguments& InArgs)
	{
		Value = InArgs._Value;
		MinValue = InArgs._MinValue;
		EditLabel = InArgs._EditLabel;
		Amount = InArgs._Amount;
		bShowAmount = InArgs._ShowAmount;
		OnValueCommitted = InArgs._OnValueCommitted;

		TSharedRef<STextBlock> TextBlock = SNew(STextBlock)
			.TextStyle(InArgs._TextStyle)
			.Text(this, &STimedDataNumericEntryBox::GetValueText);

		if (InArgs._CanEdit)
		{
			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.FillWidth(1.f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					TextBlock
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(ComboButton, SComboButton)
					.ComboButtonStyle(FTimedDataMonitorEditorStyle::Get(), "ToggleComboButton")
					.HAlign(HAlign_Center)
					.HasDownArrow(false)
					.OnGetMenuContent(this, &STimedDataNumericEntryBox::OnCreateEditMenu)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
						.Text(FEditorFontGlyphs::Pencil_Square)
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			];
		}
		else
		{
			ChildSlot
			[
				TextBlock
			];
		}
	}
	
private:
	FText GetValueText() const 
	{
		if (bShowAmount)
		{
			return FText::Format(NSLOCTEXT("TimedDataNumericEntryBox", "TimedNumericDataState", "{0}/{1}"), Amount.Get(), Value.Get());
		}
		else
		{
			return FText::AsNumber(Value.Get()); 
		}
	}

	TSharedRef<SWidget> OnCreateEditMenu()
	{
		bCloseRequested = false;

		TSharedPtr<SWidget> WidgetToFocus;
		TSharedRef<SWidget> Result = SNew(SBorder)
			.VAlign(VAlign_Center)
			.Padding(FMargin(5.f, 5.f, 5.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FTimedDataMonitorEditorStyle::Get(), "TextBlock.Regular")
					.Text(EditLabel)
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.FillWidth(1.f)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(WidgetToFocus, SNumericEntryBox<NumericType>)
					.MinValue(TOptional<NumericType>(MinValue))
					.MinDesiredValueWidth(50)
					.Value(TOptional<NumericType>(Value.Get()))
					.OnValueCommitted(this, &STimedDataNumericEntryBox::OnValueComittedCallback)
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SCheckBox)
					.Padding(4.f)
					.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked_Lambda([]() {return ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &STimedDataNumericEntryBox::CloseComboButton)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
						.Text(FEditorFontGlyphs::Check)
					]
				]
			];

		ComboButton->SetMenuContentWidgetToFocus(WidgetToFocus);
		return Result;
	}

	void CloseComboButton(ECheckBoxState InNewState)
	{
		if (ComboButton)
		{
			ComboButton->SetIsOpen(false);
		}
	}

	void OnValueComittedCallback(NumericType NewValue, ETextCommit::Type CommitType)
	{
		if (!bCloseRequested)
		{
			bCloseRequested = true;

			OnValueCommitted.ExecuteIfBound(NewValue, CommitType);
			if (ComboButton)
			{
				ComboButton->SetIsOpen(false);
			}
		}
	}

private:
	TAttribute<NumericType> Value;
	TAttribute<NumericType> Amount;
	NumericType MinValue;
	FText EditLabel;
	bool bShowAmount;
	FOnValueCommitted OnValueCommitted;
	TSharedPtr<SComboButton> ComboButton;
	bool bCloseRequested = false;
};

