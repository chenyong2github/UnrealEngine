// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimedDataMonitorEditorStyle.h"

#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"

const FName FTimedDataMonitorEditorStyle::NAME_TimecodeBrush = "Img.Timecode.Small";
const FName FTimedDataMonitorEditorStyle::NAME_PlatformTimeBrush = "Img.PlatformTime.Small";
const FName FTimedDataMonitorEditorStyle::NAME_NoEvaluationBrush = "Img.NoEvaluation.Small";


FTimedDataMonitorEditorStyle::FTimedDataMonitorEditorStyle()
	: FSlateStyleSet("TimedDataSourceEditorStyle")
{
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);

	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("TimedDataMonitor"))->GetContentDir();
	SetContentRoot(ContentDir);
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	// CheckBox
	{
		FSlateImageBrush SwitchOn = FSlateImageBrush(RootToContentDir(TEXT("Widgets/Switch_ON.png")), FVector2D(28.f, 14.f));
		FSlateImageBrush SwitchOff = FSlateImageBrush(RootToContentDir(TEXT("Widgets/Switch_OFF.png")), FVector2D(28.f, 14.f));
		FSlateImageBrush SwitchUndeterminded = FSlateImageBrush(RootToContentDir(TEXT("Widgets/Switch_Undetermined.png")), FVector2D(28.f, 14.f));

		FCheckBoxStyle SwitchStyle = FCheckBoxStyle()
			.SetForegroundColor(FLinearColor::White)
			.SetUncheckedImage(SwitchOff)
			.SetUncheckedHoveredImage(SwitchOff)
			.SetUncheckedPressedImage(SwitchOff)
			.SetUndeterminedImage(SwitchUndeterminded)
			.SetUndeterminedHoveredImage(SwitchUndeterminded)
			.SetUndeterminedPressedImage(SwitchUndeterminded)
			.SetCheckedImage(SwitchOn)
			.SetCheckedHoveredImage(SwitchOn)
			.SetCheckedPressedImage(SwitchOn)
			.SetPadding(FMargin(0, 0, 0, 1));
		Set("CheckBox.Enable", SwitchStyle);
	}

	// brush
	{
		Set("Brush.White", new FSlateColorBrush(FLinearColor::White));
	}

	// images
	{
		Set(NAME_TimecodeBrush, new FSlateImageBrush(RootToContentDir(TEXT("Widgets/Timecode_16x.png")), Icon16x16));
		Set(NAME_PlatformTimeBrush, new FSlateImageBrush(RootToContentDir(TEXT("Widgets/Time_16x.png")), Icon16x16));
		Set(NAME_NoEvaluationBrush, new FSlateImageBrush(RootToContentDir(TEXT("Widgets/NoEvaluation_16x.png")), Icon16x16));
		Set("Img.TimedDataMonitor.Small", new FSlateImageBrush(RootToContentDir(TEXT("Widgets/TimedDataMonitor_16x.png")), Icon16x16));

		Set("Img.BufferVisualization", new FSlateImageBrush(RootToContentDir(TEXT("Widgets/BufferVisualization_24x.png")), Icon24x24));
		Set("Img.Calibration", new FSlateImageBrush(RootToContentDir(TEXT("Widgets/Calibration_24x.png")), Icon24x24));
		Set("Img.TimeCorrection", new FSlateImageBrush(RootToContentDir(TEXT("Widgets/TimeCorrection_24x.png")), Icon24x24));
		Set("Img.Edit", new FSlateImageBrush(RootToContentDir(TEXT("Widgets/Edit_24x.png")), Icon24x24));
		
	}

	// font
	{
		Set("Font.Regular", FCoreStyle::GetDefaultFontStyle("Regular", FCoreStyle::RegularTextSize));
		Set("Font.Large", FCoreStyle::GetDefaultFontStyle("Regular", 12));
	}

	// text block
	{
		FTextBlockStyle NormalText = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
		Set("TextBlock.Regular", FTextBlockStyle(NormalText).SetFont(GetFontStyle("Font.Regular")));
		Set("TextBlock.Large", FTextBlockStyle(NormalText).SetFont(GetFontStyle("Font.Large")));
	}

	// TableView
	{
		const FTableRowStyle& DefaultTableRow = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
		Set("TableView.Child", FTableRowStyle(DefaultTableRow)
			.SetEvenRowBackgroundHoveredBrush(DefaultTableRow.InactiveBrush)
			.SetOddRowBackgroundHoveredBrush(DefaultTableRow.InactiveBrush)
			.SetActiveHoveredBrush(DefaultTableRow.InactiveBrush)
			.SetInactiveHoveredBrush(DefaultTableRow.InactiveBrush)
			.SetEvenRowBackgroundBrush(DefaultTableRow.InactiveBrush)
			.SetOddRowBackgroundBrush(DefaultTableRow.InactiveBrush)
			.SetActiveBrush(DefaultTableRow.InactiveBrush)
			.SetInactiveBrush(DefaultTableRow.InactiveBrush)
		);
	}

	// ComboButton
	{
		FComboButtonStyle SectionComboButton = FComboButtonStyle()
			.SetButtonStyle(
				FButtonStyle()
				.SetNormal(FSlateNoResource())
				.SetHovered(FSlateNoResource())
				.SetPressed(FSlateNoResource())
				.SetNormalPadding(FMargin(0, 0, 0, 0))
				.SetPressedPadding(FMargin(0, 1, 0, 0))
			)
			.SetDownArrowImage(FSlateNoResource())
			.SetMenuBorderBrush(FSlateNoResource());
		SectionComboButton.UnlinkColors();
		Set("FlatComboButton", SectionComboButton);

		const FCheckBoxStyle& ToggleButtonStyle = FEditorStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox");
		FComboButtonStyle ToggleComboButton = FComboButtonStyle()
			.SetButtonStyle(
				FButtonStyle()
				.SetNormal(ToggleButtonStyle.UncheckedImage)
				.SetHovered(ToggleButtonStyle.UncheckedHoveredImage)
				.SetPressed(ToggleButtonStyle.UncheckedPressedImage)
				.SetNormalPadding(FMargin(0, 0, 0, 0))
				.SetPressedPadding(FMargin(0, 1, 0, 0))
			)
			.SetDownArrowImage(FSlateNoResource())
			.SetMenuBorderBrush(FSlateNoResource());
		Set("ToggleComboButton", ToggleComboButton);
	}

	// Button
	{
		FButtonStyle FlatButton = FButtonStyle()
			.SetNormal(FSlateBoxBrush(RootToContentDir("Common/ButtonHoverHint.png"), FMargin(4 / 16.0f), FLinearColor(1, 1, 1, 0.15f)))
			.SetHovered(FSlateBoxBrush(RootToContentDir("Common/ButtonHoverHint.png"), FMargin(4 / 16.0f), FLinearColor(1, 1, 1, 0.25f)))
			.SetPressed(FSlateBoxBrush(RootToContentDir("Common/ButtonHoverHint.png"), FMargin(4 / 16.0f), FLinearColor(1, 1, 1, 0.30f)))
			.SetNormalPadding(FMargin(0, 0, 0, 1))
			.SetPressedPadding(FMargin(0, 1, 0, 0));
		Set("FlatButton", FlatButton);

		const FCheckBoxStyle& ToggleButtonStyle = FEditorStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox");
		FButtonStyle ToggleButton = FButtonStyle()
			.SetNormal(ToggleButtonStyle.UncheckedImage)
			.SetHovered(ToggleButtonStyle.UncheckedHoveredImage)
			.SetPressed(ToggleButtonStyle.UncheckedPressedImage)
			.SetNormalPadding(FMargin(0, 0, 0, 0))
			.SetPressedPadding(FMargin(0, 1, 0, 0));
		Set("ToggleButton", ToggleButton);
	}
	
	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FTimedDataMonitorEditorStyle::~FTimedDataMonitorEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FTimedDataMonitorEditorStyle& FTimedDataMonitorEditorStyle::Get()
{
	static FTimedDataMonitorEditorStyle Inst;
	return Inst;
}


