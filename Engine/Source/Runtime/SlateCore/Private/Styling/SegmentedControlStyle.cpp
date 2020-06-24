// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/SegmentedControlStyle.h"

const FName FSegmentedControlStyle::TypeName( TEXT("FSegmentedControlStyle") );

FSegmentedControlStyle::FSegmentedControlStyle()
{
}

void FSegmentedControlStyle::GetResources( TArray< const FSlateBrush* >& OutBrushes ) const
{
	ControlStyle.GetResources(OutBrushes);
	FirstControlStyle.GetResources(OutBrushes);
	LastControlStyle.GetResources(OutBrushes);
}

const FSegmentedControlStyle& FSegmentedControlStyle::GetDefault()
{
	static FSegmentedControlStyle Default;
	return Default;
}