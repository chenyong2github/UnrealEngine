// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Styling/CoreStyle.h"
#include "Framework/SlateDelegates.h"

class FArrangedChildren;
class SHorizontalBox;

/**
 * Vector Slate control
 */
template<typename NumericType>
class SNumericVectorInputBox : public SCompoundWidget
{
public:
	/** Notification for float value change */
	DECLARE_DELEGATE_OneParam(FOnNumericValueChanged, NumericType);

	/** Notification for float value committed */
	DECLARE_DELEGATE_TwoParams(FOnNumericValueCommitted, NumericType, ETextCommit::Type);

	SLATE_BEGIN_ARGS(SNumericVectorInputBox<NumericType>)
		: _Font( FAppStyle::Get().GetFontStyle("NormalFont") )
		, _AllowSpin( false )
		, _SpinDelta( 1 )
		, _bColorAxisLabels( false )
		{}
		
		/** X Component of the vector */
		SLATE_ATTRIBUTE( TOptional<NumericType>, X )

		/** Y Component of the vector */
		SLATE_ATTRIBUTE( TOptional<NumericType>, Y )

		/** Z Component of the vector */
		SLATE_ATTRIBUTE( TOptional<NumericType>, Z )

		/** Font to use for the text in this box */
		SLATE_ATTRIBUTE( FSlateFontInfo, Font )

		/** Whether or not values can be spun or if they should be typed in */
		SLATE_ARGUMENT( bool, AllowSpin )

		/** The delta amount to apply, per pixel, when the spinner is dragged. */
		SLATE_ATTRIBUTE( NumericType, SpinDelta )

		/** Should the axis labels be colored */
		SLATE_ARGUMENT( bool, bColorAxisLabels )		

		/** Allow responsive layout to crush the label and margins when there is not a lot of room */
		UE_DEPRECATED(5.0, "AllowResponsiveLayout unused as it is no longer necessary.")
		FArguments& AllowResponsiveLayout(bool bAllow)
		{
			return TSlateBaseNamedArgs<SNumericVectorInputBox<NumericType>>::Me();
		}
					
		/** Called when the x value of the vector is changed */
		SLATE_EVENT( FOnNumericValueChanged, OnXChanged )

		/** Called when the y value of the vector is changed */
		SLATE_EVENT( FOnNumericValueChanged, OnYChanged )

		/** Called when the z value of the vector is changed */
		SLATE_EVENT( FOnNumericValueChanged, OnZChanged )

		/** Called when the x value of the vector is committed */
		SLATE_EVENT( FOnNumericValueCommitted, OnXCommitted )

		/** Called when the y value of the vector is committed */
		SLATE_EVENT( FOnNumericValueCommitted, OnYCommitted )

		/** Called when the z value of the vector is committed */
		SLATE_EVENT( FOnNumericValueCommitted, OnZCommitted )

		/** Menu extender delegate for the X value */
		SLATE_EVENT( FMenuExtensionDelegate, ContextMenuExtenderX )

		/** Menu extender delegate for the Y value */
		SLATE_EVENT( FMenuExtensionDelegate, ContextMenuExtenderY )

		/** Menu extender delegate for the Z value */
		SLATE_EVENT( FMenuExtensionDelegate, ContextMenuExtenderZ )

		/** Called right before the slider begins to move for any of the vector components */
		SLATE_EVENT( FSimpleDelegate, OnBeginSliderMovement )
		
		/** Called right after the slider handle is released by the user for any of the vector components */
		SLATE_EVENT( FOnNumericValueChanged, OnEndSliderMovement )

		/** Provide custom type functionality for the vector */
		SLATE_ARGUMENT( TSharedPtr< INumericTypeInterface<NumericType> >, TypeInterface )

	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct( const FArguments& InArgs )
		{
			TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

			ChildSlot
				[
					HorizontalBox
				];

			ConstructX(InArgs, HorizontalBox);
			ConstructY(InArgs, HorizontalBox);
			ConstructZ(InArgs, HorizontalBox);
		}
private:


	/**
	 * Construct widgets for the X Value
	 */
	void ConstructX( const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox )
	{
		TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
		if (InArgs._bColorAxisLabels)
		{
			const FLinearColor LabelColor = SNumericEntryBox<NumericType>::RedLabelBackgroundColor;
			LabelWidget = SNumericEntryBox<NumericType>::BuildNarrowColorLabel(LabelColor);
		}

		TAttribute<TOptional<NumericType>> Value = InArgs._X;

		HorizontalBox->AddSlot()
			[
				SNew(SNumericEntryBox<NumericType>)
				.AllowSpin(InArgs._AllowSpin)
				.Font(InArgs._Font)
				.Value(InArgs._X)
				.OnValueChanged(InArgs._OnXChanged)
				.OnValueCommitted(InArgs._OnXCommitted)
				.ToolTipText(MakeAttributeLambda([Value]
				{
					if (Value.Get().IsSet())
					{
						return FText::Format(NSLOCTEXT("SVectorInputBox", "X_ToolTip", "X: {0}"), Value.Get().GetValue());
					}
					return NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values");
				}))
				.UndeterminedString(NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values"))
				.ContextMenuExtender(InArgs._ContextMenuExtenderX)
				.TypeInterface(InArgs._TypeInterface)
				.MinValue(TOptional<NumericType>())
				.MaxValue(TOptional<NumericType>())
				.MinSliderValue(TOptional<NumericType>())
				.MaxSliderValue(TOptional<NumericType>())
				.LinearDeltaSensitivity(1)
				.Delta(InArgs._SpinDelta)
				.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
				.OnEndSliderMovement(InArgs._OnEndSliderMovement)
				.LabelPadding(FMargin(3.f))
				.LabelLocation(SNumericEntryBox<NumericType>::ELabelLocation::Inside)
				.Label()
				[
					LabelWidget
				]
			];

	}

	/**
	 * Construct widgets for the Y Value
	 */
	void ConstructY( const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox )
	{
		TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
		if (InArgs._bColorAxisLabels)
		{
			const FLinearColor LabelColor = SNumericEntryBox<NumericType>::GreenLabelBackgroundColor;
			LabelWidget = SNumericEntryBox<NumericType>::BuildNarrowColorLabel(LabelColor);
		}

		TAttribute<TOptional<NumericType>> Value = InArgs._Y;

		HorizontalBox->AddSlot()
			[
				SNew(SNumericEntryBox<NumericType>)
				.AllowSpin(InArgs._AllowSpin)
				.Font(InArgs._Font)
				.Value(InArgs._Y)
				.OnValueChanged(InArgs._OnYChanged)
				.OnValueCommitted(InArgs._OnYCommitted)
				.ToolTipText(MakeAttributeLambda([Value]
				{
					if (Value.Get().IsSet())
					{
						return FText::Format(NSLOCTEXT("SVectorInputBox", "Y_ToolTip", "Y: {0}"), Value.Get().GetValue());
					}
					return NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values");
				}))
				.UndeterminedString(NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values"))
				.ContextMenuExtender(InArgs._ContextMenuExtenderY)
				.TypeInterface(InArgs._TypeInterface)
				.MinValue(TOptional<NumericType>())
				.MaxValue(TOptional<NumericType>())
				.MinSliderValue(TOptional<NumericType>())
				.MaxSliderValue(TOptional<NumericType>())
				.LinearDeltaSensitivity(1)
				.Delta(InArgs._SpinDelta)
				.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
				.OnEndSliderMovement(InArgs._OnEndSliderMovement)
				.LabelPadding(FMargin(3))
				.LabelLocation(SNumericEntryBox<NumericType>::ELabelLocation::Inside)
				.Label()
				[
					LabelWidget
				]
			];

	}

	/**
	 * Construct widgets for the Z Value
	 */
	void ConstructZ(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
		if (InArgs._bColorAxisLabels)
		{
			const FLinearColor LabelColor = SNumericEntryBox<NumericType>::BlueLabelBackgroundColor;
			LabelWidget = SNumericEntryBox<NumericType>::BuildNarrowColorLabel(LabelColor);
		}

		TAttribute<TOptional<NumericType>> Value = InArgs._Z;

		HorizontalBox->AddSlot()
			[
				SNew(SNumericEntryBox<NumericType>)
				.AllowSpin(InArgs._AllowSpin)
				.Font(InArgs._Font)
				.Value(InArgs._Z)
				.OnValueChanged(InArgs._OnZChanged)
				.OnValueCommitted(InArgs._OnZCommitted)
				.ToolTipText(MakeAttributeLambda([Value]
				{
					if (Value.Get().IsSet())
					{
						return FText::Format(NSLOCTEXT("SVectorInputBox", "Z_ToolTip", "Z: {0}"), Value.Get().GetValue());
					}
					return NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values");
				}))
				.UndeterminedString(NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values"))
				.ContextMenuExtender(InArgs._ContextMenuExtenderZ)
				.TypeInterface(InArgs._TypeInterface)
				.MinValue(TOptional<NumericType>())
				.MaxValue(TOptional<NumericType>())
				.MinSliderValue(TOptional<NumericType>())
				.MaxSliderValue(TOptional<NumericType>())
				.LinearDeltaSensitivity(1)
				.Delta(InArgs._SpinDelta)
				.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
				.OnEndSliderMovement(InArgs._OnEndSliderMovement)
				.LabelPadding(FMargin(3))
				.LabelLocation(SNumericEntryBox<NumericType>::ELabelLocation::Inside)
				.Label()
				[
					LabelWidget
				]
			];
	}

};

/**
 * For backward compatibility
 */
using SVectorInputBox = SNumericVectorInputBox<float>;

