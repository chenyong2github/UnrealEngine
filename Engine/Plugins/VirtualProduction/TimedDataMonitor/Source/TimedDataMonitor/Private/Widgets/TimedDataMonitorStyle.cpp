// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/TimedDataMonitorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"


FTimedDataMonitorStyle::FTimedDataMonitorStyle()
	: FSlateStyleSet("TimedDataSourceStyle")
{
	const FVector2D Icon8x8(8.0f, 8.0f);
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon32x32(32.0f, 32.0f);
	const FVector2D Icon48x48(48.0f, 48.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);

	const FLinearColor White(FLinearColor::White);
	const FLinearColor AlmostWhite( FColor(200, 200, 200) );
	const FLinearColor VeryLightGrey( FColor(128, 128, 128) );
	const FLinearColor LightGrey( FColor(96, 96, 96) );
	const FLinearColor MediumGrey( FColor(62, 62, 62) );
	const FLinearColor DarkGrey( FColor(30, 30, 30) );
	const FLinearColor Black(FLinearColor::Black);

	const FLinearColor SelectionColor( 0.728f, 0.364f, 0.003f );
	const FLinearColor SelectionColor_Subdued( 0.807f, 0.596f, 0.388f );
	const FLinearColor SelectionColor_Inactive( 0.25f, 0.25f, 0.25f );
	const FLinearColor SelectionColor_Pressed( 0.701f, 0.225f, 0.003f );

	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("TimedDataMonitor"))->GetContentDir();
	SetContentRoot(ContentDir);
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));


	// CheckBox
	{
		FSlateImageBrush SwitchOn = FSlateImageBrush(RootToContentDir(TEXT("Widgets/Switch_ON.png")), FVector2D(28.0F, 14.0F));
		FSlateImageBrush SwitchOff = FSlateImageBrush(RootToContentDir(TEXT("Widgets/Switch_OFF.png")), FVector2D(28.0F, 14.0F));
		FSlateImageBrush SwitchUndeterminded = FSlateImageBrush(RootToContentDir(TEXT("Widgets/Switch_Undetermined.png")), FVector2D(28.0F, 14.0F));

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
		Set("Img.Timecode.Small", new FSlateImageBrush(RootToContentDir(TEXT("Widgets/Timecode_16x.png")), Icon16x16));
		Set("Img.Timecode", new FSlateImageBrush(RootToContentDir(TEXT("Widgets/Timecode_64x.png")), Icon64x64));
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
	
	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FTimedDataMonitorStyle::~FTimedDataMonitorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FTimedDataMonitorStyle& FTimedDataMonitorStyle::Get()
{
	static FTimedDataMonitorStyle Inst;
	return Inst;
}


