// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

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
		.VAlign(EVerticalAlignment::VAlign_Center)
		.Padding(5,5)
		[SNew(STextBlock).Text(Text)]

		+ SHorizontalBox::Slot()
		.VAlign(EVerticalAlignment::VAlign_Center)
		.Padding(5, 5)
		[Widget];
}
