// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationWidgetHelpers.h"

#include "Internationalization/Text.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

const int32 FCameraCalibrationWidgetHelpers::DefaultRowHeight = 35;


TSharedRef<SWidget> FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(FText&& Text, TSharedRef<SWidget> Widget)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(5,5)
		.FillWidth(0.35f)
		[SNew(STextBlock).Text(Text)]

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(5, 5)
		.FillWidth(0.65f)
		[Widget];
}
