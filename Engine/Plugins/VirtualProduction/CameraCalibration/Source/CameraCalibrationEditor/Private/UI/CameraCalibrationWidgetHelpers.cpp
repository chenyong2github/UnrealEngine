// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationWidgetHelpers.h"

#include "Internationalization/Text.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

const int32 FCameraCalibrationWidgetHelpers::DefaultRowHeight = 35;

const FLinearColor FCameraCalibrationWidgetHelpers::SelectedBoxBackgroundColor = FLinearColor(FColor(0xBD, 0x8B, 0x06));
const FLinearColor FCameraCalibrationWidgetHelpers::SelectedBoxForegroundColor = FLinearColor(FColor(0xE7, 0xE7, 0xE7));

const FLinearColor FCameraCalibrationWidgetHelpers::UnselectedBoxBackgroundColor = FLinearColor(FColor(0x50, 0x50, 0x50));
const FLinearColor FCameraCalibrationWidgetHelpers::UnselectedBoxForegroundColor = FLinearColor(FColor(0xE7, 0xE7, 0xE7));

const FSlateFontInfo FCameraCalibrationWidgetHelpers::TitleFontInfo = FCoreStyle::GetDefaultFontStyle("Bold", 13);


TSharedRef<SWidget> FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(FText&& Text, TSharedRef<SWidget> Widget)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.VAlign(EVerticalAlignment::VAlign_Center)
		.Padding(5,5)
		[SNew(STextBlock).Text(Text)]

		+ SHorizontalBox::Slot()
		.VAlign(EVerticalAlignment::VAlign_Center)
		.Padding(5, 5)
		[Widget];
}
