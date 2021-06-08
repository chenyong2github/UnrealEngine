// Copyright Epic Games, Inc. All Rights Reserved.


#include "DMXFixtureComponentDouble.h"
#include "DMXFixtureActor.h"



UDMXFixtureComponentDouble::UDMXFixtureComponentDouble()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDMXFixtureComponentDouble::Initialize()
{
	Super::Initialize();

	// Two channels per cell
	for (auto& Cell : Cells)
	{
		// Init Channel interpolation  
		Cell.ChannelInterpolation.Init(FInterpolationData(), 2);

		// Init interpolation speed scale
		Cell.ChannelInterpolation[0].InterpolationScale = InterpolationScale;
		Cell.ChannelInterpolation[1].InterpolationScale = InterpolationScale;

		// Init interpolation range 
		Cell.ChannelInterpolation[0].RangeValue = FMath::Abs(DMXChannel1.MaxValue - DMXChannel1.MinValue);
		Cell.ChannelInterpolation[1].RangeValue = FMath::Abs(DMXChannel2.MaxValue - DMXChannel2.MinValue);
	}

	InitializeComponent();
}

float UDMXFixtureComponentDouble::GetDMXInterpolatedStep(int32 ChannelIndex) const
{
	if (ChannelIndex == 0)
	{
		return (CurrentCell->ChannelInterpolation[0].CurrentSpeed * CurrentCell->ChannelInterpolation[0].Direction);
	}
	else if (ChannelIndex == 1)
	{
		return (CurrentCell->ChannelInterpolation[1].CurrentSpeed * CurrentCell->ChannelInterpolation[1].Direction);
	}
	else
	{
		// Invalid index
		checkNoEntry();
	}

	return 0.f;
}

float UDMXFixtureComponentDouble::GetDMXInterpolatedValue(int32 ChannelIndex) const
{
	if (ChannelIndex == 0)
	{
		return CurrentCell->ChannelInterpolation[0].CurrentValue;
	}
	else if (ChannelIndex == 1)
	{
		return CurrentCell->ChannelInterpolation[1].CurrentValue;
	}
	else
	{
		// Invalid index
		checkNoEntry();
	}
	
	return 0.f;
}

float UDMXFixtureComponentDouble::GetDMXTargetValue(int32 ChannelIndex) const
{
	if (ChannelIndex == 0)
	{
		return CurrentCell->ChannelInterpolation[0].TargetValue;
	}
	else if (ChannelIndex == 1)
	{
		return CurrentCell->ChannelInterpolation[1].TargetValue;
	}
	else
	{
		// Invalid index
		checkNoEntry();
	}

	return 0.f;
}

bool UDMXFixtureComponentDouble::IsDMXInterpolationDone(int32 ChannelIndex) const
{
	if (ChannelIndex == 0)
	{
		return CurrentCell->ChannelInterpolation[0].IsInterpolationDone();
	}
	else if (ChannelIndex == 1)
	{
		return CurrentCell->ChannelInterpolation[1].IsInterpolationDone();
	}
	else
	{
		// Invalid index
		checkNoEntry();
	}

	return false;
}

float UDMXFixtureComponentDouble::GetInterpolatedValue(int32 ChannelIndex, float Alpha) const
{
	if (ChannelIndex == 0)
	{
		float Remapped = FMath::Lerp(DMXChannel1.MinValue, DMXChannel1.MaxValue, Alpha);
		return Remapped;
	}
	else if (ChannelIndex == 1)
	{
		float Remapped = FMath::Lerp(DMXChannel2.MinValue, DMXChannel2.MaxValue, Alpha);
		return Remapped;
	}
	else
	{
		// Invalid index
		checkNoEntry();
	}

	return -1;
}

bool UDMXFixtureComponentDouble::IsTargetValid(int32 ChannelIndex, float Target)
{
	if (ChannelIndex == 0)
	{
		return CurrentCell->ChannelInterpolation[0].IsTargetValid(Target, SkipThreshold);
	}
	else if (ChannelIndex == 1)
	{
		return CurrentCell->ChannelInterpolation[1].IsTargetValid(Target, SkipThreshold);
	}
	else
	{
		// Invalid index
		checkNoEntry();
	}
	
	return false;
}

void UDMXFixtureComponentDouble::SetTargetValue(int32 ChannelIndex, float Value)
{
	FInterpolationData& InterpolationData = ChannelIndex == 0 ? CurrentCell->ChannelInterpolation[0] : CurrentCell->ChannelInterpolation[1];

	if (!InterpolationData.bFirstValueWasSet)
	{
		InterpolationData.bFirstValueWasSet = true;

		// As per implementaion, push to make interpolation start, so a target can be set without interpolating
		InterpolationData.Push(Value);
		InterpolationData.SetTarget(Value);

		// Set the initial value here. Further values are set from the actors
		if (ChannelIndex == 0)
		{
			SetChannel1ValueNoInterp(Value);
		}
		else if (ChannelIndex == 1)
		{
			SetChannel2ValueNoInterp(Value);
		}
		else
		{
			checkNoEntry();
		}
	}
	else
	{
		if (bUseInterpolation)
		{
			InterpolationData.Push(Value);
		}
		else
		{
			InterpolationData.SetTarget(Value);
		}
	}
}	
