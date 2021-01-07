// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "SRotatorInputBox"

void SRotatorInputBox::Construct( const SRotatorInputBox::FArguments& InArgs )
{
	this->ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		[
			SNew(SNumericEntryBox<float>)
			.AllowSpin(InArgs._AllowSpin)
			.MinSliderValue(0.0f)
			.MaxSliderValue(359.999f)
			.LabelPadding(FMargin(3))
			.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
			.Label()
			[
				InArgs._bColorAxisLabels ? SNumericEntryBox<float>::BuildNarrowColorLabel(SNumericEntryBox<float>::RedLabelBackgroundColor) : SNullWidget::NullWidget
			]
			.Font( InArgs._Font )
			.Value( InArgs._Roll )
			.OnValueChanged( InArgs._OnRollChanged )
			.OnValueCommitted( InArgs._OnRollCommitted )
			.OnBeginSliderMovement( InArgs._OnBeginSliderMovement )
			.OnEndSliderMovement( InArgs._OnEndSliderMovement )
			.UndeterminedString( LOCTEXT("MultipleValues", "Multiple Values") )
			.ToolTipText_Lambda([RollAttr = InArgs._Roll]
			{
				const TOptional<float>& RollValue = RollAttr.Get();
				return RollValue.IsSet() 
					? FText::Format(LOCTEXT("Roll_ToolTip", "X(Roll): {0}"), RollValue.GetValue())
					: LOCTEXT("MultipleValues", "Multiple Values");
			})
			.TypeInterface(InArgs._TypeInterface)
		]
		+SHorizontalBox::Slot()
		[
			SNew(SNumericEntryBox<float>)
			.AllowSpin(InArgs._AllowSpin)
			.MinSliderValue(0.0f)
			.MaxSliderValue(359.999f)
			.LabelPadding(FMargin(3))
			.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
			.Label()
			[
				InArgs._bColorAxisLabels ? SNumericEntryBox<float>::BuildNarrowColorLabel(SNumericEntryBox<float>::GreenLabelBackgroundColor) : SNullWidget::NullWidget
			]
			.Font( InArgs._Font )
			.Value( InArgs._Pitch )
			.OnValueChanged( InArgs._OnPitchChanged )
			.OnValueCommitted( InArgs._OnPitchCommitted )
			.OnBeginSliderMovement( InArgs._OnBeginSliderMovement )
			.OnEndSliderMovement( InArgs._OnEndSliderMovement )
			.UndeterminedString( LOCTEXT("MultipleValues", "Multiple Values") )
			.ToolTipText_Lambda([PitchAttr = InArgs._Pitch]
			{
				const TOptional<float>& PitchValue = PitchAttr.Get();
				return PitchValue.IsSet()
					? FText::Format(LOCTEXT("Pitch_ToolTip", "Y(Pitch): {0}"), PitchValue.GetValue())
					: LOCTEXT("MultipleValues", "Multiple Values");
			})
			.TypeInterface(InArgs._TypeInterface)
		]
		+SHorizontalBox::Slot()
		[
			SNew(SNumericEntryBox<float>)
			.AllowSpin(InArgs._AllowSpin)
			.MinSliderValue(0.0f)
			.MaxSliderValue(359.999f)
			.LabelPadding(FMargin(3))
			.LabelLocation(SNumericEntryBox<float>::ELabelLocation::Inside)
			.Label()
			[
				InArgs._bColorAxisLabels ? SNumericEntryBox<float>::BuildNarrowColorLabel(SNumericEntryBox<float>::BlueLabelBackgroundColor) : SNullWidget::NullWidget
			]
			.Font( InArgs._Font )
			.Value( InArgs._Yaw )
			.OnValueChanged( InArgs._OnYawChanged )
			.OnValueCommitted( InArgs._OnYawCommitted )
			.OnBeginSliderMovement( InArgs._OnBeginSliderMovement )
			.OnEndSliderMovement( InArgs._OnEndSliderMovement )
			.UndeterminedString( LOCTEXT("MultipleValues", "Multiple Values") )
			.ToolTipText_Lambda([YawAttr = InArgs._Yaw]
			{
				const TOptional<float>& YawValue = YawAttr.Get();
				return YawValue.IsSet()
					? FText::Format(LOCTEXT("Yaw_ToolTip", "Z(Yaw): {0}"), YawValue.GetValue())
					: LOCTEXT("MultipleValues", "Multiple Values");
			})
			.TypeInterface(InArgs._TypeInterface)
		]
	];

}


#undef LOCTEXT_NAMESPACE

