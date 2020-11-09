// Copyright Epic Games, Inc. All Rights Reserved.


#include "DMXFixtureComponentColor.h"

UDMXFixtureComponentColor::UDMXFixtureComponentColor()
	: CurrentTargetColorRef(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
	BitResolution = 255;
	InitCells(1);
}


// NB: Does not support interpolation
void UDMXFixtureComponentColor::InitCells(int NCells)
{
	TargetColorArray.Init(FLinearColor(1, 1, 1, 1), NCells);
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

// Set bit resolution based on DMX signal format mapping
// assuming each channel uses the same bit resolution: checking only first one
void UDMXFixtureComponentColor::SetBitResolution(TMap<FDMXAttributeName, EDMXFixtureSignalFormat> Map)
{
	EDMXFixtureSignalFormat* Format = Map.Find(ChannelName1);
	if (Format != nullptr)
	{
		switch (*Format)
		{
			case(EDMXFixtureSignalFormat::E8Bit): BitResolution = 255; break;
			case(EDMXFixtureSignalFormat::E16Bit): BitResolution = 65535; break;
			case(EDMXFixtureSignalFormat::E24Bit): BitResolution = 16777215; break;
			case(EDMXFixtureSignalFormat::E32Bit): BitResolution = 4294967295; break;
			default: BitResolution = 255;
		}
	}
}


