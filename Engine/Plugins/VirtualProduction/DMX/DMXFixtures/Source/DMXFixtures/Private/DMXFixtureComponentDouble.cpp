// Copyright Epic Games, Inc. All Rights Reserved.


#include "DMXFixtureComponentDouble.h"
#include "DMXFixtureActor.h"

UDMXFixtureComponentDouble::UDMXFixtureComponentDouble()
{
	PrimaryComponentTick.bCanEverTick = false;
	NumChannels = 2;
	ChannelRefs.Add(&DMXChannel1);
	ChannelRefs.Add(&DMXChannel2);
	InitCells(1);
}

void UDMXFixtureComponentDouble::InitCells(int NCells)
{
	Cells.Init(FCell(), NCells);
	CurrentCell = &Cells[0];

	// 2 channels per cell
	for (auto& Cell : Cells)
	{
		Cell.ChannelInterpolation.Init(FInterpolationData(), NumChannels);
	}
}

float UDMXFixtureComponentDouble::RemapValue(int ChannelIndex, float Alpha)
{
	if (ChannelIndex < NumChannels)
	{
		float Remapped = FMath::Lerp(ChannelRefs[ChannelIndex]->MinValue, ChannelRefs[ChannelIndex]->MaxValue, Alpha);
		return Remapped;
	}
	else
	{
		return -1;
	}
}

// Propagate RangeValue into ChannelInterpolation data
void UDMXFixtureComponentDouble::SetRangeValue()
{
	for (auto& Cell : Cells)
	{
		for (int ChannelIndex=0; ChannelIndex < ChannelRefs.Num(); ChannelIndex++)
		{
			Cell.ChannelInterpolation[ChannelIndex].RangeValue = FMath::Abs(ChannelRefs[ChannelIndex]->MaxValue - ChannelRefs[ChannelIndex]->MinValue);
		}
	}
}

bool UDMXFixtureComponentDouble::IsTargetValid(int ChannelIndex, float Target)
{
	if (ChannelIndex < NumChannels)
	{
		return CurrentCell->ChannelInterpolation[ChannelIndex].IsTargetValid(Target, SkipThreshold);
	}
	else
	{
		return false;
	}
}

void UDMXFixtureComponentDouble::Push(int ChannelIndex, float Target)
{
	if (ChannelIndex < NumChannels)
	{
		CurrentCell->ChannelInterpolation[ChannelIndex].Push(Target);
	}
}

void UDMXFixtureComponentDouble::SetTarget(int ChannelIndex, float Target)
{
	if (ChannelIndex < NumChannels)
	{
		CurrentCell->ChannelInterpolation[ChannelIndex].SetTarget(Target);
	}
}

float UDMXFixtureComponentDouble::DMXInterpolatedValue(int ChannelIndex)
{
	if (ChannelIndex < NumChannels)
	{
		return CurrentCell->ChannelInterpolation[ChannelIndex].CurrentValue;
	}
	else
	{
		return -1;
	}
}

float UDMXFixtureComponentDouble::DMXInterpolatedStep(int ChannelIndex)
{
	if (ChannelIndex < NumChannels)
	{
		return (CurrentCell->ChannelInterpolation[ChannelIndex].CurrentSpeed * CurrentCell->ChannelInterpolation[ChannelIndex].Direction);
	}
	else
	{
		return -1;
	}
}

float UDMXFixtureComponentDouble::DMXTargetValue(int ChannelIndex)
{
	if (ChannelIndex < NumChannels)
	{
		return CurrentCell->ChannelInterpolation[ChannelIndex].TargetValue;
	}
	else
	{
		return -1;
	}
}

bool UDMXFixtureComponentDouble::DMXIsInterpolationDone(int ChannelIndex)
{
	if (ChannelIndex < NumChannels)
	{
		return CurrentCell->ChannelInterpolation[ChannelIndex].IsInterpolationDone();
	}
	else
	{
		return false;
	}
}