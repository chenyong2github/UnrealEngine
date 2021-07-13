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
	struct FSlot : public TSlotBase<FSlot>, public TAlignmentWidgetSlotMixin<FSlot>
	{
		FSlot(const OptionType& InValue)
			: TSlotBase<FSlot>()
			, TAlignmentWidgetSlotMixin<FSlot>(HAlign_Center, VAlign_Fill)
			, _Text()
			, _Tooltip()
			, _Icon(nullptr)
			, _Value(InValue)
			{ }

		SLATE_SLOT_BEGIN_ARGS_OneMixin(FSlot, TSlotBase<FSlot>, TAlignmentWidgetSlotMixin<FSlot>)
			SLATE_ATTRIBUTE(FText, Text)
			SLATE_ATTRIBUTE(FText, ToolTip)
			SLATE_ATTRIBUTE(const FSlateBrush*, Icon)
			SLATE_ARGUMENT(TOptional<OptionType>, Value)
		SLATE_SLOT_END_ARGS()

		void Construct(const FChildren& SlotOwner, FSlotArguments&& InArgs)
		{
			TSlotBase<FSlot>::Construct(SlotOwner, MoveTemp(InArgs));
			TAlignmentWidgetSlotMixin<FSlot>::ConstructMixin(SlotOwner, MoveTemp(InArgs));
			if (InArgs._Text.IsSet())
			{
				_Text = MoveTemp(InArgs._Text);
			}
			if (InArgs._ToolTip.IsSet())
			{
				_Tooltip = MoveTemp(InArgs._ToolTip);
			}
			if (InArgs._Icon.IsSet())
			{
				_Icon = MoveTemp(InArgs._Icon);
			}
			if (InArgs._Value.IsSet())
			{
				_Value = MoveTemp(InArgs._Value.GetValue());
			}
		}

		void SetText(TAttribute<FText> InText)
		{
			_Text = MoveTemp(InText);
		}

		FText GetText() const
		{
			return _Text.Get();
		}

		void SetIcon(TAttribute<const FSlateBrush*> InBrush)
		{
			_Icon = MoveTemp(InBrush);
		}

		const FSlateBrush* GetIcon() const
		{
			return _Icon.Get();
		}

		void SetToolTip(TAttribute<FText> InTooltip)
		{
			_Tooltip = MoveTemp(InTooltip);
		}

		FText GetToolTip() const
		{
			return _Tooltip.Get();
		}

	friend SSegmentedControl<OptionType>;

	private:
		TAttribute<FText> _Text;
		TAttribute<FText> _Tooltip;
		TAttribute<const FSlateBrush*> _Icon;

		OptionType _Value;
		TWeakPtr<SCheckBox> _CheckBox;
	};

	static typename FSlot::FSlotArguments Slot(const OptionType& InValue)
	{
		return typename FSlot::FSlotArguments(MakeUnique<FSlot>(InValue));
	}

	DECLARE_DELEGATE_OneParam( FOnValueChanged, OptionType );

	SLATE_BEGIN_ARGS( SSegmentedControl<OptionType> )
		: _Style(&FAppStyle::Get().GetWidgetStyle<FSegmentedControlStyle>("SegmentedControl"))
		, _TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("ButtonText"))
		, _MaxSegmentsPerLine(0)
	{}
		/** Slot type supported by this panel */
		SLATE_SLOT_ARGUMENT(FSlot, Slots)

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

	SSegmentedControl()
		: Children(this)
		, CurrentValue(*this)
	{}

	void Construct( const FArguments& InArgs )
	{
		check(InArgs._Style);

		Style = InArgs._Style;
		TextStyle = InArgs._TextStyle;

		CurrentValueIsBound = InArgs._Value.IsBound();
		CurrentValue.Assign(*this, InArgs._Value);
		OnValueChanged = InArgs._OnValueChanged;

		UniformPadding = InArgs._UniformPadding;

		MaxSegmentsPerLine = InArgs._MaxSegmentsPerLine;
		Children.AddSlots(MoveTemp(const_cast<TArray<typename FSlot::FSlotArguments>&>(InArgs._Slots)));
		RebuildChildren();
	}

	void RebuildChildren()
	{
		TSharedPtr<SUniformGridPanel> UniformBox = SNew(SUniformGridPanel);

		const int32 NumSlots = Children.Num();
		for ( int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex )
		{
			TSharedRef<SWidget> Child = Children[SlotIndex].GetWidget();
			FSlot* ChildSlotPtr = &Children[SlotIndex];
			const OptionType ChildValue = ChildSlotPtr->_Value;

			TAttribute<FVector2D> SpacerLambda = FVector::ZeroVector;
			if (ChildSlotPtr->_Icon.IsBound() || ChildSlotPtr->_Text.IsBound())
			{
				SpacerLambda = MakeAttributeLambda([ChildSlotPtr]() { return (ChildSlotPtr->_Icon.Get() != nullptr && !ChildSlotPtr->_Text.Get().IsEmpty()) ? FVector2D(8.0f, 1.0f) : FVector2D::ZeroVector; });
			}
			else
			{
				SpacerLambda = (ChildSlotPtr->_Icon.Get() != nullptr && !ChildSlotPtr->_Text.Get().IsEmpty()) ? FVector2D(8.0f, 1.0f) : FVector2D::ZeroVector;
			}

			if (Child == SNullWidget::NullWidget)
			{
				Child = SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(ChildSlotPtr->_Icon)
				]	

				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpacer)
					.Size(SpacerLambda)
				]

				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.5f, 0.f, 0.f)  // Compensate down for the baseline - helps when using all caps
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(TextStyle)
					.Justification(ETextJustify::Center)
					.Text(ChildSlotPtr->_Text) 
				];
			}

			int32 ColumnIndex = MaxSegmentsPerLine ? SlotIndex % MaxSegmentsPerLine : SlotIndex;
			UniformBox->AddSlot(ColumnIndex, MaxSegmentsPerLine > 0 ? SlotIndex / MaxSegmentsPerLine : 0)
			// Note HAlignment is applied at the check box level because if it were applied here it would make the slots look physically disconnected from each other 
			.VAlign(ChildSlotPtr->GetVerticalAlignment())
			[
				SAssignNew(ChildSlotPtr->_CheckBox, SCheckBox)
				.Clipping(EWidgetClipping::ClipToBounds)
				.HAlign(ChildSlotPtr->GetHorizontalAlignment())
				.ToolTipText(ChildSlotPtr->_Tooltip)
				.Style(ColumnIndex == 0 ? &Style->FirstControlStyle : ColumnIndex == (NumSlots - 1) ? &Style->LastControlStyle : &Style->ControlStyle)
				.IsChecked(GetCheckBoxStateAttribute(ChildValue))
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
	using FScopedWidgetSlotArguments = typename TPanelChildren<FSlot>::FScopedWidgetSlotArguments;
	FScopedWidgetSlotArguments AddSlot(const OptionType& InValue, bool bRebuildChildren = true)
	{
		if (bRebuildChildren)
		{
			TWeakPtr<SSegmentedControl> AsWeak = SharedThis(this);
			return FScopedWidgetSlotArguments { MakeUnique<FSlot>(InValue), this->Children, INDEX_NONE, [AsWeak](const FSlot*, int32)
				{
					if (TSharedPtr<SSegmentedControl> SharedThis = AsWeak.Pin())
					{
						SharedThis->RebuildChildren();
					}
				}};
		}
		else
		{
			return FScopedWidgetSlotArguments{ MakeUnique<FSlot>(InValue), this->Children, INDEX_NONE };
		}
	}

	int32 NumSlots() const
	{
		return Children.Num();
	}

	OptionType GetValue() const
	{
		return CurrentValue.Get();
	}

	/** See the Value attribute */
	void SetValue(TAttribute<OptionType> InValue) 
	{
		CurrentValueIsBound = InValue.IsBound();
		CurrentValue.Assign(*this, MoveTemp(InValue));

		if (!CurrentValueIsBound)
		{
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				const FSlot& Slot = Children[Index];
				if (TSharedPtr<SCheckBox> CheckBox = Slot._CheckBox.Pin())
				{
					CheckBox->SetIsChecked(Slot._Value == CurrentValue.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
				}
			}
		}
	}

private:
	TAttribute<ECheckBoxState> GetCheckBoxStateAttribute(OptionType InValue) const
	{
		auto Lambda = [this, InValue]()
		{
			return InValue == CurrentValue.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		};

		if (CurrentValueIsBound)
		{
			return MakeAttributeLambda(Lambda);
		}

		return Lambda();
	}

	void CommitValue(const ECheckBoxState InCheckState, OptionType InValue)
	{
		if (InCheckState == ECheckBoxState::Checked)
		{
			// don't overwrite the bound attribute, but still notify that the value was committed.
			if (!CurrentValueIsBound)
			{
				CurrentValue.Set(*this, InValue);
				for (int32 Index = 0; Index < Children.Num(); ++Index)
				{
					const FSlot& Slot = Children[Index];
					if (TSharedPtr<SCheckBox> CheckBox = Slot._CheckBox.Pin())
					{
						CheckBox->SetIsChecked(Slot._Value == CurrentValue.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
					}
			    }
			}

			OnValueChanged.ExecuteIfBound( InValue );
		}
	}

private:
	TPanelChildren<FSlot> Children;

	FOnValueChanged OnValueChanged;

	TSlateAttribute<OptionType, EInvalidateWidgetReason::Paint> CurrentValue;

	TAttribute<FMargin> UniformPadding;

	const FSegmentedControlStyle* Style;

	const FTextBlockStyle* TextStyle;

	int32 MaxSegmentsPerLine = 0;

	bool CurrentValueIsBound = false;
};
