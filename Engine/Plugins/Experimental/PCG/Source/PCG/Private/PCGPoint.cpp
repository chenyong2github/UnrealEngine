// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPoint.h"

FPCGPoint::FPCGPoint(const FTransform& InTransform, float InDensity, int32 InSeed)
	: Transform(InTransform)
	, Density(InDensity)
	, Seed(InSeed)
{
}

FBoxSphereBounds FPCGPoint::GetBounds() const
{
	return FBoxSphereBounds(FBox(BoundsMin, BoundsMax).TransformBy(Transform));
}

FBoxSphereBounds FPCGPoint::GetDensityBounds() const
{
	return FBoxSphereBounds(FBox((2 - Steepness) * BoundsMin, (2 - Steepness) * BoundsMax).TransformBy(Transform));
}