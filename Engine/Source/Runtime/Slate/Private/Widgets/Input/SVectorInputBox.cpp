// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "SVectorInputBox"

void SVectorInputBox::Construct(const FArguments& InArgs)
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

void SVectorInputBox::ConstructX(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
{
	TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
	if(InArgs._bColorAxisLabels)
	{
		const FLinearColor LabelColor = SNumericEntryBox<float>::RedLabelBackgroundColor;
		LabelWidget = SNumericEntryBox<float>::BuildNarrowColorLabel(LabelColor);
	}

	TAttribute<TOptional<float>> Value = InArgs._X;

	HorizontalBox->AddSlot()
	[
		SNew(SNumericEntryBox<float>)
		.AllowSpin(InArgs._AllowSpin)
		.Font(InArgs._Font)
		.Value(InArgs._X)
		.OnValueChanged(InArgs._OnXChanged)
		.OnValueCommitted(InArgs._OnXCommitted)
		.ToolTipText(MakeAttributeLambda([Value]
		{
			if (Value.Get().IsSet())
			{
				return FText::Format(LOCTEXT("X_ToolTip", "X: {0}"), Value.Get().GetValue());
			}
			return LOCTEXT("MultipleValues", "Multiple Values");
		}))
		.UndeterminedString(LOCTEXT("MultipleValues", "Multiple Values"))
		.ContextMenuExtender(InArgs._ContextMenuExtenderX)
		.TypeInterface(InArgs._TypeInterface)
		.MinValue(TOptional<float>())
		.MaxValue(TOptional<float>())
		.MinSliderValue(TOptional<float>())
		.MaxSliderValue(TOptional<float>())
		.LinearDeltaSensitivity(1)
		.Delta(InArgs._SpinDelta)
		.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
		.OnEndSliderMovement(InArgs._OnEndSliderMovement)
		.LabelPadding(FMargin(3))
		.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
		.Label()
		[
			LabelWidget
		]
	];

}

void SVectorInputBox::ConstructY(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
{
	TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
	if (InArgs._bColorAxisLabels)
	{
		const FLinearColor LabelColor = SNumericEntryBox<float>::GreenLabelBackgroundColor;
		LabelWidget = SNumericEntryBox<float>::BuildNarrowColorLabel(LabelColor);
	}

	TAttribute<TOptional<float>> Value = InArgs._Y;

	HorizontalBox->AddSlot()
	[
		SNew(SNumericEntryBox<float>)
		.AllowSpin(InArgs._AllowSpin)
		.Font(InArgs._Font)
		.Value(InArgs._Y)
		.OnValueChanged(InArgs._OnYChanged)
		.OnValueCommitted(InArgs._OnYCommitted)
		.ToolTipText(MakeAttributeLambda([Value]
		{
			if (Value.Get().IsSet())
			{
					return FText::Format(LOCTEXT("Y_ToolTip", "Y: {0}"), Value.Get().GetValue());
			}
			return LOCTEXT("MultipleValues", "Multiple Values");
		}))
		.UndeterminedString(LOCTEXT("MultipleValues", "Multiple Values"))
		.ContextMenuExtender(InArgs._ContextMenuExtenderY)
		.TypeInterface(InArgs._TypeInterface)
		.MinValue(TOptional<float>())
		.MaxValue(TOptional<float>())
		.MinSliderValue(TOptional<float>())
		.MaxSliderValue(TOptional<float>())
		.LinearDeltaSensitivity(1)
		.Delta(InArgs._SpinDelta)
		.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
		.OnEndSliderMovement(InArgs._OnEndSliderMovement)
		.LabelPadding(FMargin(3))
		.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
		.Label()
		[
			LabelWidget
		]
];

}

void SVectorInputBox::ConstructZ(const FArguments& InArgs, TSharedRef<SHorizontalBox> HorizontalBox)
{
	TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
	if (InArgs._bColorAxisLabels)
	{
		const FLinearColor LabelColor = SNumericEntryBox<float>::BlueLabelBackgroundColor;
		LabelWidget = SNumericEntryBox<float>::BuildNarrowColorLabel(LabelColor);
	}

	TAttribute<TOptional<float>> Value = InArgs._Z;

	HorizontalBox->AddSlot()
	[
		SNew(SNumericEntryBox<float>)
		.AllowSpin(InArgs._AllowSpin)
		.Font(InArgs._Font)
		.Value(InArgs._Z)
		.OnValueChanged(InArgs._OnZChanged)
		.OnValueCommitted(InArgs._OnZCommitted)
		.ToolTipText(MakeAttributeLambda([Value]
		{
			if (Value.Get().IsSet())
			{
				return FText::Format(LOCTEXT("Z_ToolTip", "Z: {0}"), Value.Get().GetValue());
			}
			return LOCTEXT("MultipleValues", "Multiple Values");
		}))
		.UndeterminedString(LOCTEXT("MultipleValues", "Multiple Values"))
		.ContextMenuExtender(InArgs._ContextMenuExtenderZ)
		.TypeInterface(InArgs._TypeInterface)
		.MinValue(TOptional<float>())
		.MaxValue(TOptional<float>())
		.MinSliderValue(TOptional<float>())
		.MaxSliderValue(TOptional<float>())
		.LinearDeltaSensitivity(1)
		.Delta(InArgs._SpinDelta)
		.OnBeginSliderMovement(InArgs._OnBeginSliderMovement)
		.OnEndSliderMovement(InArgs._OnEndSliderMovement)
		.LabelPadding(FMargin(3))
		.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
		.Label()
		[
			LabelWidget
		]
	];
}



#undef LOCTEXT_NAMESPACE
