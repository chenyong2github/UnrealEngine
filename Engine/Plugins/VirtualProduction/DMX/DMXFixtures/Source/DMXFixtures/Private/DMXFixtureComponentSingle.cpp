// Copyright Epic Games, Inc. All Rights Reserved.


#include "DMXFixtureComponentSingle.h"
#include "DMXFixtureActor.h"


UDMXFixtureComponentSingle::UDMXFixtureComponentSingle()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDMXFixtureComponentSingle::Initialize()
{
	Super::Initialize();

	// 1 channel per cell
	for (auto& Cell : Cells)
	{
		// Init Channel interpolation  
		Cell.ChannelInterpolation.Init(FInterpolationData(), 1);

		// Init interpolation speed scale
		Cell.ChannelInterpolation[0].InterpolationScale = InterpolationScale;

		// Init interpolation range 
		Cell.ChannelInterpolation[0].RangeValue = FMath::Abs(DMXChannel.MaxValue - DMXChannel.MinValue);
	}

	InitializeComponent();
}

float UDMXFixtureComponentSingle::GetDMXInterpolatedStep() const
{
	return (CurrentCell->ChannelInterpolation[0].CurrentSpeed * CurrentCell->ChannelInterpolation[0].Direction);
}

float UDMXFixtureComponentSingle::GetDMXInterpolatedValue() const
{
	return CurrentCell->ChannelInterpolation[0].CurrentValue;
}

float UDMXFixtureComponentSingle::GetDMXTargetValue() const
{
	return CurrentCell->ChannelInterpolation[0].TargetValue;
}

bool UDMXFixtureComponentSingle::IsDMXInterpolationDone() const
{
	return CurrentCell->ChannelInterpolation[0].IsInterpolationDone();
}

float UDMXFixtureComponentSingle::GetInterpolatedValue(float Alpha) const
{
	float Remapped = FMath::Lerp(DMXChannel.MinValue, DMXChannel.MaxValue, Alpha);

	return Remapped;
}

bool UDMXFixtureComponentSingle::IsTargetValid(float Target)
{
	return CurrentCell->ChannelInterpolation[0].IsTargetValid(Target, SkipThreshold);
}

void UDMXFixtureComponentSingle::SetTargetValue(float Value)
{
	if (!CurrentCell->ChannelInterpolation[0].bFirstValueWasSet)
	{
		CurrentCell->ChannelInterpolation[0].bFirstValueWasSet = true;

		// As per implementaion, push to make interpolation start, so a target can be set without interpolating
		CurrentCell->ChannelInterpolation[0].Push(Value);
		CurrentCell->ChannelInterpolation[0].SetTarget(Value);

		SetValueNoInterp(Value);
	}
	else
	{
		if (bUseInterpolation)
		{
			CurrentCell->ChannelInterpolation[0].Push(Value);
		}
		else
		{
			CurrentCell->ChannelInterpolation[0].SetTarget(Value);
		}
	}
}

void UDMXFixtureComponentSingle::Push(float Target)
{
	// DEPRECATED 4.27
	CurrentCell->ChannelInterpolation[0].Push(Target);
}

void UDMXFixtureComponentSingle::SetTarget(float Target)
{
	// DEPRECATED 4.27
	CurrentCell->ChannelInterpolation[0].SetTarget(Target);
}
