// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/SegmentedControlStyle.h"
#include "Brushes/SlateNoResource.h"

const FName FSegmentedControlStyle::TypeName( TEXT("FSegmentedControlStyle") );

FSegmentedControlStyle::FSegmentedControlStyle()
	: BackgroundBrush(FSlateNoResource())
	, UniformPadding(0)
{
}

void FSegmentedControlStyle::GetResources( TArray< const FSlateBrush* >& OutBrushes ) const
{
	OutBrushes.Add(&BackgroundBrush);
	ControlStyle.GetResources(OutBrushes);
	FirstControlStyle.GetResources(OutBrushes);
	LastControlStyle.GetResources(OutBrushes);
}

const FSegmentedControlStyle& FSegmentedControlStyle::GetDefault()
{
	static FSegmentedControlStyle Default;
	return Default;
}