// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Transform.isph"

inline static void GrowToInclude(uniform FVector &Min, uniform FVector &Max, const uniform FVector& V)
{
	Min = VectorMin(Min, V);
	Max = VectorMax(Max, V);
}

export void TransformedAABB(const uniform FTransform &SpaceTransform, const uniform FVector &Min, const uniform FVector &Max, uniform FVector &NewMin, uniform FVector &NewMax)
{
	const uniform FVector CurrentExtents = Max - Min;

	uniform FVector MinToNewSpace = TransformPosition(SpaceTransform, Min);
	uniform FVector MaxToNewSpace = MinToNewSpace;
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max));

	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Min + ForwardVector * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max - ForwardVector * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Min + RightVector * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max - RightVector * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Min + UpVector * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max - UpVector * CurrentExtents));

	NewMin = MinToNewSpace;
	NewMax = MaxToNewSpace;
}
