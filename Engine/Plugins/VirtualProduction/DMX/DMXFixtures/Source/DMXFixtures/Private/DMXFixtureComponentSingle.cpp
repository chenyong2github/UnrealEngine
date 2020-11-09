// Copyright Epic Games, Inc. All Rights Reserved.


#include "DMXFixtureComponentSingle.h"
#include "DMXFixtureActor.h"

UDMXFixtureComponentSingle::UDMXFixtureComponentSingle()
{
	PrimaryComponentTick.bCanEverTick = false;
	NumChannels = 1;
	InitCells(1);
}

void UDMXFixtureComponentSingle::InitCells(int NCells)
{
	Cells.Init(FCell(), NCells);
	CurrentCell = &Cells[0];

	// 1 channel per cell
	for (auto& Cell : Cells)
	{
		Cell.ChannelInterpolation.Init(FInterpolationData(), NumChannels);
	}
}

float UDMXFixtureComponentSingle::RemapValue(float Alpha)
{
	float Remapped = FMath::Lerp(DMXChannel.MinValue, DMXChannel.MaxValue, Alpha);

	return Remapped;
}

bool UDMXFixtureComponentSingle::IsTargetValid(float Target)
{
	return CurrentCell->ChannelInterpolation[0].IsTargetValid(Target, SkipThreshold);
}

void UDMXFixtureComponentSingle::Push(float Target)
{
	CurrentCell->ChannelInterpolation[0].Push(Target);
}

void UDMXFixtureComponentSingle::SetTarget(float Target)
{
	CurrentCell->ChannelInterpolation[0].SetTarget(Target);
}

void UDMXFixtureComponentSingle::SetRangeValue()
{
	for (auto& Cell : Cells)
	{
		Cell.ChannelInterpolation[0].RangeValue = FMath::Abs(DMXChannel.MaxValue - DMXChannel.MinValue);
	}
}

float UDMXFixtureComponentSingle::DMXInterpolatedValue()
{
	return CurrentCell->ChannelInterpolation[0].CurrentValue;
}

float UDMXFixtureComponentSingle::DMXInterpolatedStep()
{
	return (CurrentCell->ChannelInterpolation[0].CurrentSpeed * CurrentCell->ChannelInterpolation[0].Direction);
}

float UDMXFixtureComponentSingle::DMXTargetValue()
{
	return CurrentCell->ChannelInterpolation[0].TargetValue;
}

bool UDMXFixtureComponentSingle::DMXIsInterpolationDone()
{
	return CurrentCell->ChannelInterpolation[0].IsInterpolationDone();
}