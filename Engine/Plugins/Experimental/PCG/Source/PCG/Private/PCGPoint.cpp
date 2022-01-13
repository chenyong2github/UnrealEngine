// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPoint.h"

FPCGPoint::FPCGPoint(const FTransform& InTransform, float InDensity, int32 InSeed)
	: Transform(InTransform), Density(InDensity), Seed(InSeed)
{
	// TODO: this is a placeholder
	Extents = FVector::One();
}

FBoxSphereBounds FPCGPoint::GetBounds() const
{
	return FBoxSphereBounds(FBox::BuildAABB(FVector::Zero(), Extents).TransformBy(Transform));
}