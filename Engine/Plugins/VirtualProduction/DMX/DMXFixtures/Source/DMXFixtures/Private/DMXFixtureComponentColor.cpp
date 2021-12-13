// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixtureComponentColor.h"


UDMXFixtureComponentColor::UDMXFixtureComponentColor()
	: CurrentTargetColorRef(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDMXFixtureComponentColor::Initialize()
{
	Super::Initialize();

	const FLinearColor& DefaultColor = FLinearColor::White;
	TargetColorArray.Init(DefaultColor, Cells.Num());
	
	CurrentTargetColorRef = &TargetColorArray[0];

	InitializeComponent();

	SetColorNoInterp(DefaultColor);
}

void UDMXFixtureComponentColor::SetCurrentCell(int Index)
{
	if (CurrentTargetColorRef &&
		TargetColorArray.IsValidIndex(Index) &&
		Index < TargetColorArray.Num())
	{
		CurrentTargetColorRef = &TargetColorArray[Index];
	}
}

bool UDMXFixtureComponentColor::IsColorValid(const FLinearColor& NewColor) const
{
	if (CurrentTargetColorRef &&
		!CurrentTargetColorRef->Equals(NewColor, SkipThreshold))
	{
		return true;
	}

	return false;
}

void UDMXFixtureComponentColor::SetTargetColor(const FLinearColor& NewColor)
{
	if (CurrentTargetColorRef &&
		IsColorValid(NewColor))
	{
		// Never interpolated
		CurrentTargetColorRef->R = NewColor.R;
		CurrentTargetColorRef->G = NewColor.G;
		CurrentTargetColorRef->B = NewColor.B;
		CurrentTargetColorRef->A = NewColor.A;

		SetColorNoInterp(NewColor);
	}
}
