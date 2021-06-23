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

	TargetColorArray.Init(FLinearColor(1.f, 1.f, 1.f, 0.f), Cells.Num());
	CurrentTargetColorRef = &TargetColorArray[0];

	InitializeComponent();
}

void UDMXFixtureComponentColor::SetCurrentCell(int Index)
{
	if (Index < TargetColorArray.Num())
	{
		CurrentTargetColorRef = &TargetColorArray[Index];
	}
}

bool UDMXFixtureComponentColor::IsColorValid(const FLinearColor& NewColor) const
{
	if (!CurrentTargetColorRef->Equals(NewColor, SkipThreshold))
	{
		return true;
	}

	return false;
}

void UDMXFixtureComponentColor::SetTargetColor(const FLinearColor& NewColor)
{
	CurrentTargetColorRef->R = NewColor.R;
	CurrentTargetColorRef->G = NewColor.G;
	CurrentTargetColorRef->B = NewColor.B;
	CurrentTargetColorRef->A = NewColor.A;
}
