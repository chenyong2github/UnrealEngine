// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPoint.h"

FPCGPoint::FPCGPoint(const FTransform& InTransform, float InDensity, int32 InSeed)
	: Transform(InTransform)
	, Density(InDensity)
	, Extents(FVector::One())
	, Color(FVector4::One())
	, Seed(InSeed)
{
}

FBoxSphereBounds FPCGPoint::GetBounds() const
{
	return FBoxSphereBounds(FBox::BuildAABB(FVector::Zero(), Extents).TransformBy(Transform));
}