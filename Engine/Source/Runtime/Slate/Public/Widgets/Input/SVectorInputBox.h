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

#include <type_traits>

class FArrangedChildren;
class SHorizontalBox;

/**
 * Vector Slate control
 */
template<typename NumericType, typename VectorType = UE::Math::TVector<NumericType>, int32 NumberOfComponents = 3>
class SNumericVectorInputBox : public SCompoundWidget
{
public:
	/** Notification for float value change */
	DECLARE_DELEGATE_OneParam(FOnNumericValueChanged, NumericType);

	/** Notification for float value committed */
	DECLARE_DELEGATE_TwoParams(FOnNumericValueCommitted, NumericType, ETextCommit::Type);

	struct FArguments;

private:
	using ThisClass = SNumericVectorInputBox<NumericType, VectorType, NumberOfComponents>;

	struct FVectorXArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorXArguments : FVectorXArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		/** X Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, X)

		/** Called when the x value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnXChanged)

		/** Called when the x value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnXCommitted)

		/** Menu extender delegate for the X value */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtenderX)
	};

	struct FVectorYArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorYArguments : FVectorYArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		/** Y Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, Y)

		/** Called when the Y value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnYChanged)

		/** Called when the Y value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnYCommitted)

		/** Menu extender delegate for the Y value */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtenderY)
	};

	struct FVectorZArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorZArguments : FVectorZArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		/** Z Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, Z)

		/** Called when the Z value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnZChanged)

		/** Called when the Z value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnZCommitted)

		/** Menu extender delegate for the Z value */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtenderZ)
	};

	struct FVectorWArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorWArguments : FVectorWArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		/** W Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, W)

		/** Called when the W value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnWChanged)

		/** Called when the W value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnWCommitted)

		/** Menu extender delegate for the W value */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtenderW)
	};

public:
	//SLATE_BEGIN_ARGS(SNumericVectorInputBox<NumericType>)
	struct FArguments : public TSlateBaseNamedArgs<ThisClass>
		, std::conditional<NumberOfComponents >= 1, FVectorXArguments<FArguments>, FVectorXArgumentsEmpty>::type
		, std::conditional<NumberOfComponents >= 2, FVectorYArguments<FArguments>, FVectorYArgumentsEmpty>::type
		, std::conditional<NumberOfComponents >= 3, FVectorZArguments<FArguments>, FVectorZArgumentsEmpty>::type
		, std::conditional<NumberOfComponents >= 4, FVectorWArguments<FArguments>, FVectorWArgumentsEmpty>::type
	{
		typedef FArguments WidgetArgsType;
		FORCENOINLINE FArguments()
			: _Font(FAppStyle::Get().GetFontStyle("NormalFont"))
			, _AllowSpin(false)
			, _SpinDelta(1)
			, _bColorAxisLabels(false)
		{}

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
			return TSlateBaseNamedArgs<ThisClass>::Me();
		}

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
	void Construct(const FArguments& InArgs)
	{
		TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

		ChildSlot
		[
			HorizontalBox
		];

		if constexpr (NumberOfComponents >= 1)
		{
			ConstructX(InArgs, HorizontalBox);
		}
		if constexpr (NumberOfComponents >= 2)
		{
			ConstructY(InArgs, HorizontalBox);
		}
		if constexpr (NumberOfComponents >= 3)
		{
			ConstructZ(InArgs, HorizontalBox);
		}
		if constexpr (NumberOfComponents >= 4)
		{
			ConstructW(InArgs, HorizontalBox);
		}
	}

private:
	/**
	 * Construct the widget component
	 */
	void ConstructComponent(const FArguments& InArgs,
		const FLinearColor& LabelColor,
		const FText& TooltipText,
		TSharedRef<SHorizontalBox>& HorizontalBox,
		const TAttribute<TOptional<NumericType>>& Component,
		const FOnNumericValueChanged& OnComponentChanged,
		const FOnNumericValueCommitted& OnComponentCommitted,
		const FMenuExtensionDelegate& OnContextMenuExtenderComponent)
	{
		TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
		if (InArgs._bColorAxisLabels)
		{
			LabelWidget = SNumericEntryBox<NumericType>::BuildNarrowColorLabel(LabelColor);
		}

		TAttribute<TOptional<NumericType>> Value = Component;

		HorizontalBox->AddSlot()
		[
			SNew(SNumericEntryBox<NumericType>)
			.AllowSpin(InArgs._AllowSpin)
			.Font(InArgs._Font)
			.Value(Component)
			.OnValueChanged(OnComponentChanged)
			.OnValueCommitted(OnComponentCommitted)
			.ToolTipText(MakeAttributeLambda([Component, TooltipText]
			{
				if (Component.Get().IsSet())
				{
					return FText::Format(TooltipText, Component.Get().GetValue());
				}
				return NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values");
			}))
			.UndeterminedString(NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values"))
			.ContextMenuExtender(OnContextMenuExtenderComponent)
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
	 * Construct widgets for the X Value
	 */
	void ConstructX(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(InArgs,
			SNumericEntryBox<NumericType>::RedLabelBackgroundColor,
			NSLOCTEXT("SVectorInputBox", "X_ToolTip", "X: {0}"),
			HorizontalBox,
			InArgs._X,
			InArgs._OnXChanged,
			InArgs._OnXCommitted,
			InArgs._ContextMenuExtenderX
		);
	}

	/**
	 * Construct widgets for the Y Value
	 */
	void ConstructY(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(InArgs,
			SNumericEntryBox<NumericType>::GreenLabelBackgroundColor,
			NSLOCTEXT("SVectorInputBox", "Y_ToolTip", "Y: {0}"),
			HorizontalBox,
			InArgs._Y,
			InArgs._OnYChanged,
			InArgs._OnYCommitted,
			InArgs._ContextMenuExtenderY
		);
	}

	/**
	 * Construct widgets for the Z Value
	 */
	void ConstructZ(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(InArgs,
			SNumericEntryBox<NumericType>::BlueLabelBackgroundColor,
			NSLOCTEXT("SVectorInputBox", "Z_ToolTip", "Z: {0}"),
			HorizontalBox,
			InArgs._Z,
			InArgs._OnZChanged,
			InArgs._OnZCommitted,
			InArgs._ContextMenuExtenderZ
		);
	}

	/**
	 * Construct widgets for the W Value
	 */
	void ConstructW(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(InArgs,
			FLinearColor::Yellow,
			NSLOCTEXT("SVectorInputBox", "W_ToolTip", "W: {0}"),
			HorizontalBox,
			InArgs._W,
			InArgs._OnWChanged,
			InArgs._OnWCommitted,
			InArgs._ContextMenuExtenderW
		);
	}
};

/**
 * For backward compatibility
 */
using SVectorInputBox = SNumericVectorInputBox<float, UE::Math::TVector<float>, 3>;

