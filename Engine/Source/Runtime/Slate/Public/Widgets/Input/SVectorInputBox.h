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
	/** Notification for numeric value change */
	DECLARE_DELEGATE_OneParam(FOnNumericValueChanged, NumericType);

	/** Notification for numeric value committed */
	DECLARE_DELEGATE_TwoParams(FOnNumericValueCommitted, NumericType, ETextCommit::Type);

	/** Notification for vector value change */
	DECLARE_DELEGATE_OneParam(FOnVectorValueChanged, VectorType);

	/** Notification for vector value committed */
	DECLARE_DELEGATE_TwoParams(FOnVectorValueCommitted, VectorType, ETextCommit::Type);

	/** Delegate to constrain the vector during a change */
	DECLARE_DELEGATE_ThreeParams(FOnConstrainVector, int32 /* Component */, VectorType /* old */ , VectorType& /* new */);

	struct FArguments;

private:
	using ThisClass = SNumericVectorInputBox<NumericType, VectorType, NumberOfComponents>;

	struct FVectorXArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorXArguments : FVectorXArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		FORCENOINLINE FVectorXArguments()
			: _ToggleXChecked(ECheckBoxState::Checked)
		{}

		/** X Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, X)

		/** Called when the x value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnXChanged)

		/** Called when the x value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnXCommitted)

		/** The value of the toggle X checkbox */
		SLATE_ATTRIBUTE( ECheckBoxState, ToggleXChecked )

		/** Called whenever the toggle X changes state */
		SLATE_EVENT( FOnCheckStateChanged, OnToggleXChanged )

		/** Menu extender delegate for the X value */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtenderX)
	};

	struct FVectorYArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorYArguments : FVectorYArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		FORCENOINLINE FVectorYArguments()
			: _ToggleYChecked(ECheckBoxState::Checked)
		{}

		/** Y Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, Y)

		/** Called when the Y value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnYChanged)

		/** Called when the Y value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnYCommitted)

		/** The value of the toggle Y checkbox */
		SLATE_ATTRIBUTE( ECheckBoxState, ToggleYChecked )

		/** Called whenever the toggle Y changes state */
		SLATE_EVENT( FOnCheckStateChanged, OnToggleYChanged )

		/** Menu extender delegate for the Y value */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtenderY)
	};

	struct FVectorZArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorZArguments : FVectorZArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		FORCENOINLINE FVectorZArguments()
			: _ToggleZChecked(ECheckBoxState::Checked)
		{}

		/** Z Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, Z)

		/** Called when the Z value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnZChanged)

		/** Called when the Z value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnZCommitted)

		/** The value of the toggle Z checkbox */
		SLATE_ATTRIBUTE( ECheckBoxState, ToggleZChecked )

		/** Called whenever the toggle Z changes state */
		SLATE_EVENT( FOnCheckStateChanged, OnToggleZChanged )

		/** Menu extender delegate for the Z value */
		SLATE_EVENT(FMenuExtensionDelegate, ContextMenuExtenderZ)
	};

	struct FVectorWArgumentsEmpty {};
	template<typename ArgumentType>
	struct FVectorWArguments : FVectorWArgumentsEmpty
	{
		using WidgetArgsType = ArgumentType;

		FORCENOINLINE FVectorWArguments()
			: _ToggleWChecked(ECheckBoxState::Checked)
		{}

		/** W Component of the vector */
		SLATE_ATTRIBUTE(TOptional<NumericType>, W)

		/** Called when the W value of the vector is changed */
		SLATE_EVENT(FOnNumericValueChanged, OnWChanged)

		/** Called when the W value of the vector is committed */
		SLATE_EVENT(FOnNumericValueCommitted, OnWCommitted)

		/** The value of the toggle W checkbox */
		SLATE_ATTRIBUTE( ECheckBoxState, ToggleWChecked )

		/** Called whenever the toggle W changes state */
		SLATE_EVENT( FOnCheckStateChanged, OnToggleWChanged )

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
			: _EditableTextBoxStyle( &FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox") )
			, _SpinBoxStyle(&FAppStyle::Get().GetWidgetStyle<FSpinBoxStyle>("NumericEntrySpinBox") )
			, _Font(FAppStyle::Get().GetFontStyle("NormalFont"))
			, _AllowSpin(false)
			, _SpinDelta(1)
			, _bColorAxisLabels(false)
			, _DisplayToggle(false)
			, _TogglePadding(FMargin(1.f,0.f,1.f,0.f) )
		{}

		/** Optional Value of the vector */
		SLATE_ATTRIBUTE(TOptional<VectorType>, Vector)

		/** Optional minimum value of the vector */
		SLATE_ATTRIBUTE(TOptional<VectorType>, MinVector)

		/** Optional maximum value of the vector */
		SLATE_ATTRIBUTE(TOptional<VectorType>, MaxVector)

		/** Optional minimum (slider) value of the vector */
		SLATE_ATTRIBUTE(TOptional<VectorType>, MinSliderVector)

		/** Optional maximum (slider) value of the vector */
		SLATE_ATTRIBUTE(TOptional<VectorType>, MaxSliderVector)

		/** Called when the vector is changed */
		SLATE_EVENT(FOnVectorValueChanged, OnVectorChanged)

		/** Called when the vector is committed */
		SLATE_EVENT(FOnVectorValueCommitted, OnVectorCommitted)

		/** Style to use for the editable text box within this widget */
		SLATE_STYLE_ARGUMENT( FEditableTextBoxStyle, EditableTextBoxStyle )

		/** Style to use for the spin box within this widget */
		SLATE_STYLE_ARGUMENT( FSpinBoxStyle, SpinBoxStyle )
		
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

		/** Whether or not to include a toggle checkbox to the left of the widget */
		SLATE_ARGUMENT( bool, DisplayToggle )
			
		/** Padding around the toggle checkbox */
		SLATE_ARGUMENT( FMargin, TogglePadding )

		/** Delegate to constrain the vector */
		SLATE_ARGUMENT( FOnConstrainVector, ConstrainVector )
		
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
	void ConstructComponent(int32 ComponentIndex,
		const FArguments& InArgs,
		const FLinearColor& LabelColor,
		const FText& TooltipText,
		TSharedRef<SHorizontalBox>& HorizontalBox,
		const TAttribute<TOptional<NumericType>>& Component,
		const FOnNumericValueChanged& OnComponentChanged,
		const FOnNumericValueCommitted& OnComponentCommitted,
		const TAttribute<ECheckBoxState> ToggleChecked,
		const FOnCheckStateChanged& OnToggleChanged,
		const FMenuExtensionDelegate& OnContextMenuExtenderComponent)
	{
		TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
		if (InArgs._bColorAxisLabels)
		{
			LabelWidget = SNumericEntryBox<NumericType>::BuildNarrowColorLabel(LabelColor);
		}

		TAttribute<TOptional<NumericType>> Value = CreatePerComponentGetter(ComponentIndex, Component, InArgs._Vector);

		HorizontalBox->AddSlot()
		[
			SNew(SNumericEntryBox<NumericType>)
			.AllowSpin(InArgs._AllowSpin)
			.EditableTextBoxStyle(InArgs._EditableTextBoxStyle)
			.SpinBoxStyle(InArgs._SpinBoxStyle)
			.Font(InArgs._Font)
			.Value(Value)
			.OnValueChanged(CreatePerComponentChanged(ComponentIndex, OnComponentChanged, InArgs._Vector, InArgs._OnVectorChanged, InArgs._ConstrainVector))
			.OnValueCommitted(CreatePerComponentCommitted(ComponentIndex, OnComponentCommitted, InArgs._Vector, InArgs._OnVectorCommitted, InArgs._ConstrainVector))
			.ToolTipText(MakeAttributeLambda([Value, TooltipText]
			{
				if (Value.Get().IsSet())
				{
					return FText::Format(TooltipText, Value.Get().GetValue());
				}
				return NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values");
			}))
			.UndeterminedString(NSLOCTEXT("SVectorInputBox", "MultipleValues", "Multiple Values"))
			.ContextMenuExtender(OnContextMenuExtenderComponent)
			.TypeInterface(InArgs._TypeInterface)
			.MinValue(CreatePerComponentGetter(ComponentIndex, TOptional<NumericType>(), InArgs._MinVector))
			.MaxValue(CreatePerComponentGetter(ComponentIndex, TOptional<NumericType>(), InArgs._MaxVector))
			.MinSliderValue(CreatePerComponentGetter(ComponentIndex, TOptional<NumericType>(), InArgs._MinSliderVector))
			.MaxSliderValue(CreatePerComponentGetter(ComponentIndex, TOptional<NumericType>(), InArgs._MaxSliderVector))
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
			.DisplayToggle(InArgs._DisplayToggle)
			.TogglePadding(InArgs._TogglePadding)
			.ToggleChecked(ToggleChecked)
			.OnToggleChanged(OnToggleChanged)
		];
	}

	/**
	 * Construct widgets for the X Value
	 */
	void ConstructX(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(0,
			InArgs,
			SNumericEntryBox<NumericType>::RedLabelBackgroundColor,
			NSLOCTEXT("SVectorInputBox", "X_ToolTip", "X: {0}"),
			HorizontalBox,
			InArgs._X,
			InArgs._OnXChanged,
			InArgs._OnXCommitted,
			InArgs._ToggleXChecked,
			InArgs._OnToggleXChanged,
			InArgs._ContextMenuExtenderX
		);
	}

	/**
	 * Construct widgets for the Y Value
	 */
	void ConstructY(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(1,
			InArgs,
			SNumericEntryBox<NumericType>::GreenLabelBackgroundColor,
			NSLOCTEXT("SVectorInputBox", "Y_ToolTip", "Y: {0}"),
			HorizontalBox,
			InArgs._Y,
			InArgs._OnYChanged,
			InArgs._OnYCommitted,
			InArgs._ToggleYChecked,
			InArgs._OnToggleYChanged,
			InArgs._ContextMenuExtenderY
		);
	}

	/**
	 * Construct widgets for the Z Value
	 */
	void ConstructZ(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(2,
			InArgs,
			SNumericEntryBox<NumericType>::BlueLabelBackgroundColor,
			NSLOCTEXT("SVectorInputBox", "Z_ToolTip", "Z: {0}"),
			HorizontalBox,
			InArgs._Z,
			InArgs._OnZChanged,
			InArgs._OnZCommitted,
			InArgs._ToggleZChecked,
			InArgs._OnToggleZChanged,
			InArgs._ContextMenuExtenderZ
		);
	}

	/**
	 * Construct widgets for the W Value
	 */
	void ConstructW(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
	{
		ConstructComponent(3,
			InArgs,
			SNumericEntryBox<NumericType>::LilacLabelBackgroundColor,
			NSLOCTEXT("SVectorInputBox", "W_ToolTip", "W: {0}"),
			HorizontalBox,
			InArgs._W,
			InArgs._OnWChanged,
			InArgs._OnWCommitted,
			InArgs._ToggleWChecked,
			InArgs._OnToggleWChanged,
			InArgs._ContextMenuExtenderW
		);
	}

	/*
	 * Creates a lambda to retrieve a component off a vector
	 */
	TAttribute<TOptional<NumericType>> CreatePerComponentGetter(
		int32 ComponentIndex,
		const TAttribute<TOptional<NumericType>>& Component,
		const TAttribute<TOptional<VectorType>>& Vector)
	{
		if(Vector.IsBound() || Vector.IsSet())
		{
			return TAttribute<TOptional<NumericType>>::CreateLambda(
				[ComponentIndex, Component, Vector]() -> TOptional<NumericType>
				{
					const TOptional<VectorType> OptionalVectorValue = Vector.Get();
					if(OptionalVectorValue.IsSet())
					{
						return OptionalVectorValue.GetValue()[ComponentIndex];
					}
					return Component.Get();
				});
		}
		return Component;
	}		

	/*
	 * Creates a lambda to react to a change event
	 */
	FOnNumericValueChanged CreatePerComponentChanged(
		int32 ComponentIndex,
		const FOnNumericValueChanged OnComponentChanged,
		const TAttribute<TOptional<VectorType>>& Vector,
		const FOnVectorValueChanged OnVectorValueChanged,
		const FOnConstrainVector ConstrainVector)
	{
		if(OnVectorValueChanged.IsBound())
		{
			return FOnNumericValueChanged::CreateLambda(
				[ComponentIndex, OnComponentChanged, Vector, OnVectorValueChanged, ConstrainVector](NumericType InValue)
				{
					OnComponentChanged.ExecuteIfBound(InValue);
					
					const TOptional<VectorType> OptionalVectorValue = Vector.Get();
					if(OptionalVectorValue.IsSet())
					{
						VectorType VectorValue = OptionalVectorValue.GetValue();
						VectorValue[ComponentIndex] = InValue;

						if(ConstrainVector.IsBound())
						{
							ConstrainVector.Execute(ComponentIndex, OptionalVectorValue.GetValue(), VectorValue);
						}

						OnVectorValueChanged.Execute(VectorValue);
					}
				});
		}
		return OnComponentChanged;
	}		

	/*
	 * Creates a lambda to react to a commit event
	 */
	FOnNumericValueCommitted CreatePerComponentCommitted(
		int32 ComponentIndex,
		const FOnNumericValueCommitted OnComponentCommitted,
		const TAttribute<TOptional<VectorType>>& Vector,
		const FOnVectorValueCommitted OnVectorValueCommitted,
		const FOnConstrainVector ConstrainVector)
	{
		if(OnVectorValueCommitted.IsBound())
		{
			return FOnNumericValueCommitted::CreateLambda(
				[ComponentIndex, OnComponentCommitted, Vector, OnVectorValueCommitted, ConstrainVector](NumericType InValue, ETextCommit::Type CommitType)
				{
					OnComponentCommitted.ExecuteIfBound(InValue, CommitType);
					
					const TOptional<VectorType> OptionalVectorValue = Vector.Get();
					if(OptionalVectorValue.IsSet())
					{
						VectorType VectorValue = OptionalVectorValue.GetValue();
						VectorValue[ComponentIndex] = InValue;

						if(ConstrainVector.IsBound())
						{
							ConstrainVector.Execute(ComponentIndex, OptionalVectorValue.GetValue(), VectorValue);
						}

						OnVectorValueCommitted.Execute(VectorValue, CommitType);
					}
				});
		}
		return OnComponentCommitted;
	}		
};

/**
 * For backward compatibility
 */
using SVectorInputBox = SNumericVectorInputBox<float, UE::Math::TVector<float>, 3>;

