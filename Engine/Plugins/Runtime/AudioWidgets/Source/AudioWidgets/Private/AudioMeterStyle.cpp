// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMeterStyle.h"


FAudioMeterStyle::FAudioMeterStyle()
	: MeterSize(FVector2D(100.0f, 500.0f))
	, MeterPadding(FVector2D(5.0f, 5.0f))
	, MeterValuePadding(2.0f)
	, PeakValueWidth(8.0f)
	, ValueRangeDb(FVector2D(-60, 10))
	, bShowScale(true)
	, bScaleSide(true)
	, ScaleHashOffset(5.0f)
	, ScaleHashWidth(3.0f)
	, ScaleHashHeight(20.0f)
	, DecibelsPerHash(5)
{
}

void FAudioMeterStyle::GetResources(TArray< const FSlateBrush* >& OutBrushes) const
{
	OutBrushes.Add(&MeterValueImage);
	OutBrushes.Add(&MeterBackgroundImage);
	OutBrushes.Add(&MeterPeakImage);
}

const FName FAudioMeterStyle::TypeName(TEXT("FAudioMeterStyle"));

const FAudioMeterStyle& FAudioMeterStyle::GetDefault()
{
	static FAudioMeterStyle Default;
	return Default;
}

