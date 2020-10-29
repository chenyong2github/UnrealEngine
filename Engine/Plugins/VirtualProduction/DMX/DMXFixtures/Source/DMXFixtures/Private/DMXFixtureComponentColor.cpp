// Copyright Epic Games, Inc. All Rights Reserved.


#include "DMXFixtureComponentColor.h"

UDMXFixtureComponentColor::UDMXFixtureComponentColor()
	: CurrentTargetColorRef(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
	InitCells(1);
}


// NB: Does not support interpolation
void UDMXFixtureComponentColor::InitCells(int NCells)
{
	TargetColorArray.Init(FLinearColor(1.f, 1.f, 1.f, 0.f), NCells);
	CurrentTargetColorRef = &TargetColorArray[0];
}

// NB: Does not support interpolation
void UDMXFixtureComponentColor::SetCurrentCell(int Index)
{
	if (Index < TargetColorArray.Num())
	{
		CurrentTargetColorRef = &TargetColorArray[Index];
	}
}

bool UDMXFixtureComponentColor::IsColorValid(FLinearColor NewColor)
{
	if (!CurrentTargetColorRef->Equals(NewColor, SkipThreshold))
	{
		return true;
	}
	else
	{
		return false;
	}
}

void UDMXFixtureComponentColor::SetTargetColor(FLinearColor NewColor)
{
	CurrentTargetColorRef->R = NewColor.R;
	CurrentTargetColorRef->G = NewColor.G;
	CurrentTargetColorRef->B = NewColor.B;
	CurrentTargetColorRef->A = NewColor.A;
}
