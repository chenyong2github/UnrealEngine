// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGBlueprintHelpers.h"
#include "PCGHelpers.h"
#include "PCGSettings.h"

int UPCGBlueprintHelpers::ComputeSeedFromPosition(const FVector& InPosition)
{
	// TODO: should have a config to drive this
	return PCGHelpers::ComputeSeed((int)InPosition.X, (int)InPosition.Y, (int)InPosition.Z);
}

void UPCGBlueprintHelpers::SetSeedFromPosition(FPCGPoint& InPoint)
{
	InPoint.Seed = ComputeSeedFromPosition(InPoint.Transform.GetLocation());
}

FRandomStream UPCGBlueprintHelpers::GetRandomStream(const FPCGPoint& InPoint, const UPCGSettings* OptionalSettings)
{
	if (OptionalSettings)
	{
		return FRandomStream(PCGHelpers::ComputeSeed(InPoint.Seed, OptionalSettings->Seed));
	}
	else
	{
		return FRandomStream(InPoint.Seed);
	}
}