// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWarningOrErrorBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"

void SWarningOrErrorBox::Construct(const FArguments& InArgs)
{
	SBorder::Construct(SBorder::FArguments()
		.Padding(16.0f)
		.ForegroundColor(FAppStyle::Get().GetSlateColor("Colors.White"))
		.BorderImage(InArgs._MessageStyle == EMessageStyle::Warning ? FAppStyle::Get().GetBrush("RoundedWarning") : FAppStyle::Get().GetBrush("RoundedError"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(24,24))
				.Image(InArgs._MessageStyle == EMessageStyle::Warning ? FAppStyle::Get().GetBrush("Icons.Warning") : FAppStyle::Get().GetBrush("Icons.Error"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InArgs._Message)
				.ColorAndOpacity(FAppStyle::Get().GetSlateColor("Colors.White"))
				.AutoWrapText(true)
			]
		]);
}

