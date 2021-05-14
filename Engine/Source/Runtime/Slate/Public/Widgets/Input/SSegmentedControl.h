// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/AppStyle.h"
#include "Styling/SegmentedControlStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SUniformGridPanel.h"

/**
 * A Slate Segmented Control is functionally similar to a group of Radio Buttons.
 * Slots require a templated value to return when the segment is selected by the user.
 * Users can specify text, icon or provide custom content to each Segment.
 *
 * Note: It is currently not possible to add segments after initialization 
 *  (i.e. there is no AddSlot). 
 */

template< typename OptionType >
class SSegmentedControl : public SCompoundWidget
{

public:

	/** Stores the per-child info for this panel type */
	template< typename SlotOptionType >
	struct FSlot : public TSlotBase<FSlot<SlotOptionType>>, TAlignmentWidgetSlotMixin<FSlot<SlotOptionType>>
	{
		FSlot(const SlotOptionType& InValue) 
		: TSlotBase<FSlot<SlotOptionType>>()
		, TAlignmentWidgetSlotMixin<FSlot<SlotOptionType>>(HAlign_Center, VAlign_Fill)
		, _Text()
		, _Tooltip()
		, _Icon(nullptr)
		, _Value(InValue)
		{ }

		FSlot<SlotOptionType>& Text(const TAttribute<FText>& InText)
		{
			_Text = InText;
			return *this;
		}

		FSlot<SlotOptionType>& Icon(const TAttribute<const FSlateBrush*>& InBrush)
		{
			_Icon= InBrush;
			return *this;
		}

		FSlot<SlotOptionType>& ToolTip(const TAttribute<FText>& InTooltip)
		{
			_Tooltip = InTooltip;
			return *this;
		}

	friend class SSegmentedControl<SlotOptionType>;

	protected:

		TAttribute<FText> _Text;
		TAttribute<FText> _Tooltip;
		TAttribute<const FSlateBrush*> _Icon;

		SlotOptionType _Value;
	};

	static FSlot<OptionType>& Slot(const OptionType& InValue)
	{
		return *(new FSlot<OptionType>(InValue));
	}

	DECLARE_DELEGATE_OneParam( FOnValueChanged, OptionType );

	SLATE_BEGIN_ARGS( SSegmentedControl<OptionType> )
		: _Style(&FAppStyle::Get().GetWidgetStyle<FSegmentedControlStyle>("SegmentedControl"))
		, _TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("ButtonText"))
		, _MaxSegmentsPerLine(0)
	{}
		/** Slot type supported by this panel */
		SLATE_SUPPORTS_SLOT(FSlot<OptionType>)

		/** Styling for this control */
		SLATE_STYLE_ARGUMENT(FSegmentedControlStyle, Style)
	
		/** Styling for the text in each slot. If a custom widget is supplied for a slot this argument is not used */
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)

		/** The current control value */
		SLATE_ATTRIBUTE(OptionType, Value)

		/** Padding to apply to each slot */
		SLATE_ATTRIBUTE(FMargin, UniformPadding)

		/** Called when the value is changed */
		SLATE_EVENT(FOnValueChanged, OnValueChanged)
		
		/** Optional maximum number of segments per line before the control wraps vertically to the next line. If this value is <= 0 no wrapping happens */
		SLATE_ARGUMENT(int32, MaxSegmentsPerLine)
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs )
	{
		check(InArgs._Style);

		Style = InArgs._Style;
		TextStyle = InArgs._TextStyle;

		CurrentValue = InArgs._Value;
		OnValueChanged = InArgs._OnValueChanged;

		UniformPadding = InArgs._UniformPadding;

		MaxSegmentsPerLine = InArgs._MaxSegmentsPerLine;

		const int32 NumSlots = InArgs.Slots.Num();
		Children.Reserve(NumSlots);
		for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
		{
			Children.Add(InArgs.Slots[SlotIndex]);
		}
		RebuildChildren();
	}

	void RebuildChildren()
	{
		TSharedPtr<SUniformGridPanel> UniformBox =
			SNew(SUniformGridPanel);

		const int32 NumSlots = Children.Num();
		for ( int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex )
		{
			TSharedRef<SWidget> Child = Children[SlotIndex].GetWidget();
			FSlot<OptionType>* ChildSlotPtr = &Children[SlotIndex];
			OptionType& ChildValue = ChildSlotPtr->_Value;

			if (Child == SNullWidget::NullWidget)
			{
				Child = SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image_Lambda([ChildSlotPtr] () { return ChildSlotPtr->_Icon.Get(); })
				]	

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpacer)
					.Size_Lambda( [ChildSlotPtr] () { return (ChildSlotPtr->_Icon.Get() != nullptr && !ChildSlotPtr->_Text.Get().IsEmpty()) ? FVector2D(8.0f, 1.0f) : FVector2D::ZeroVector; })
				]

				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.5f, 0.f, 0.f)  // Compensate down for the baseline - helps when using all caps
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(TextStyle)
					.Justification(ETextJustify::Center)
					.Text_Lambda([ChildSlotPtr] () { return ChildSlotPtr->_Text.Get(); } ) 
				];
			}

			int32 ColumnIndex = MaxSegmentsPerLine ? SlotIndex % MaxSegmentsPerLine : SlotIndex;
			UniformBox->AddSlot(ColumnIndex, MaxSegmentsPerLine > 0 ? SlotIndex / MaxSegmentsPerLine : 0)
			// Note HAlignment is applied at the check box level because if it were applied here it would make the slots look physically disconnected from each other 
			.VAlign(ChildSlotPtr->VAlignment)
			[
				SNew(SCheckBox)
				.Clipping(EWidgetClipping::ClipToBounds)
				.HAlign(ChildSlotPtr->HAlignment)
				.ToolTipText(ChildSlotPtr->_Tooltip)
				.Style(ColumnIndex == 0 ? &Style->FirstControlStyle : ColumnIndex == (NumSlots - 1) ? &Style->LastControlStyle : &Style->ControlStyle)
				.IsChecked(this, &SSegmentedControl::IsCurrentValue, ChildValue)
				.OnCheckStateChanged(this, &SSegmentedControl::CommitValue, ChildValue)
				.Padding(UniformPadding)
				[
					Child
				]
			];
		}

		ChildSlot
		[
			UniformBox.ToSharedRef()
		];

	}

	// Slot Management
	FSlot<OptionType>& AddSlot(const OptionType& InValue, bool bRebuildChildren = true)
	{
		FSlot<OptionType>& NewSlot = *(new FSlot<OptionType>(InValue));

		Children.Add( &NewSlot );

		if (bRebuildChildren)
		{
			RebuildChildren();
		}
		return NewSlot;
	}

	int32 NumSlots() const
	{
		return Children.Num();
	}

	/** See the Value attribute */
	OptionType& GetValue() const { return CurrentValue.Get(); }
	void SetValue(const TAttribute<OptionType>& InValue) 
	{
		CurrentValue = InValue; 
	}

	ECheckBoxState IsCurrentValue(OptionType InValue) const
	{
		return InValue == CurrentValue.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}	

	void CommitValue(const ECheckBoxState InCheckState, OptionType InValue) 
	{
		if (InCheckState == ECheckBoxState::Checked)
		{
			// don't overwrite the bound attribute, but still notify that a
			if (!CurrentValue.IsBound())
			{
				CurrentValue.Set(InValue);
			}

			OnValueChanged.ExecuteIfBound( InValue );
		}
	}


protected:
	TIndirectArray<FSlot<OptionType>> Children;

	FOnValueChanged OnValueChanged;

	TAttribute<OptionType> CurrentValue;

	TAttribute<FMargin> UniformPadding;

	const FSegmentedControlStyle* Style;

	const FTextBlockStyle* TextStyle;

	int32 MaxSegmentsPerLine = 0;
};
