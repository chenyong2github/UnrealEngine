// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "SlateOptMacros.h"

#include "EditorFontGlyphs.h"
#include "Styling/AppStyle.h"
#include "TimedDataMonitorEditorStyle.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SNullWidget.h"
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
			, _SuffixWidget(SNullWidget::NullWidget)
			, _CanEdit(true)
			, _Amount(0)
			, _ShowAmount(false)
			, _TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText")))
			, _ComboButton(true)
		{ }
		SLATE_ATTRIBUTE(NumericType, Value)
		SLATE_ARGUMENT(TOptional<NumericType>, MinValue)
		SLATE_ARGUMENT(TOptional<NumericType>, MaxValue)
		SLATE_ARGUMENT(FText, EditLabel)
		SLATE_ARGUMENT(TSharedRef<SWidget>, SuffixWidget)
		SLATE_ARGUMENT(bool, CanEdit);
		SLATE_ATTRIBUTE(NumericType, Amount);
		SLATE_ARGUMENT(bool, ShowAmount);
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ARGUMENT(bool, ComboButton)
		SLATE_EVENT(FOnValueCommitted, OnValueCommitted)
	SLATE_END_ARGS()
	
public:
	void Construct(const FArguments& InArgs)
	{
		Value = InArgs._Value;
		CachedValue = Value.Get();
		MinValue = InArgs._MinValue;
		MaxValue = InArgs._MaxValue;
		SuffixWidget = InArgs._SuffixWidget;
		Amount = InArgs._Amount;
		bShowAmount = InArgs._ShowAmount;
		bComboButton = InArgs._ComboButton;
		OnValueCommitted = InArgs._OnValueCommitted;

		TSharedRef<SWidget> TextBlock = SNew(STextBlock)
			.TextStyle(InArgs._TextStyle)
			.Text(this, &STimedDataNumericEntryBox::GetValueText);

		if (InArgs._ComboButton)
		{
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
							.Font(FAppStyle::Get().GetFontStyle("FontAwesome.8"))
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
		else
		{

			if (InArgs._CanEdit)
			{
				ChildSlot
				[
					OnCreateEditMenu()
				];
			}
			else
			{
				ChildSlot
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.TextStyle(FTimedDataMonitorEditorStyle::Get(), "TextBlock.Regular")
						.Text(InArgs._EditLabel)
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						TextBlock
					]
				];
			}
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
		TSharedRef<SWidget> InnerWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.Padding(2.0f, 0.f)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FTimedDataMonitorEditorStyle::Get(), "FlatButton")
				.OnClicked(this, &STimedDataNumericEntryBox::OnMinusClicked)
				.Content()
				[
					SNew(SImage)
					.Image(FTimedDataMonitorEditorStyle::Get().GetBrush("MinusButton"))
				]
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.FillWidth(1.f)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(EntryBox, SNumericEntryBox<NumericType>)
				.MinValue(MinValue)
				.MaxValue(MaxValue)
				.MinDesiredValueWidth(50)
				.Value_Lambda([this]() 
				{
					CachedValue = Value.Get();
					return TOptional<NumericType>(Value.Get()); 
				})
				.OnValueCommitted(this, &STimedDataNumericEntryBox::OnValueComittedCallback)

			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FTimedDataMonitorEditorStyle::Get(), "FlatButton")
				.OnClicked(this, &STimedDataNumericEntryBox::OnPlusClicked)
				.Content()
				[
					SNew(SImage)
					.Image(FTimedDataMonitorEditorStyle::Get().GetBrush("PlusButton"))
				]
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SuffixWidget
			];


		if (bComboButton)
		{
			EditMenuContent = SNew(SBorder)
				.VAlign(VAlign_Center)
				.Padding(FMargin(5.f, 5.f, 5.f, 5.f))
				[
					InnerWidget
				];
		}
		else
		{
			EditMenuContent = InnerWidget;
		}
		
		return EditMenuContent.ToSharedRef();
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
		if (MinValue.IsSet())
		{
			NewValue = FMath::Max(MinValue.GetValue(), NewValue);
		}

		if (MaxValue.IsSet())
		{
			NewValue = FMath::Min(MaxValue.GetValue(), NewValue);
		}

		CachedValue = NewValue;
		OnValueCommitted.ExecuteIfBound(NewValue, CommitType);
	}

	FReply OnMinusClicked()
	{
		if (EntryBox)
		{
			CachedValue = Value.Get();
			if (!MinValue.IsSet() || CachedValue - 1 >= MinValue.GetValue())
			{
				CachedValue--;
			}
			OnValueCommitted.ExecuteIfBound(CachedValue, ETextCommit::Type::Default);
		}
		return FReply::Handled();
	}

	FReply OnPlusClicked()
	{
		if (EntryBox)
		{
			CachedValue = Value.Get();
			if (!MaxValue.IsSet() || CachedValue + 1 <= MinValue.GetValue())
			{
				CachedValue++;
			}
			OnValueCommitted.ExecuteIfBound(CachedValue, ETextCommit::Type::Default);
		}
		return FReply::Handled();
	}

private:
	TAttribute<NumericType> Value;
	TAttribute<NumericType> Amount;
	NumericType CachedValue;
	TSharedPtr<SNumericEntryBox<NumericType>> EntryBox;
	TOptional<NumericType> MinValue;
	TOptional<NumericType> MaxValue;
	FText EditLabel;
	TSharedRef<SWidget> SuffixWidget = SNullWidget::NullWidget;
	bool bShowAmount;
	FOnValueCommitted OnValueCommitted;
	TSharedPtr<SComboButton> ComboButton;
	bool bComboButton = true;
	bool bCloseRequested = false;
	TSharedPtr<SWidget> EditMenuContent;
};

