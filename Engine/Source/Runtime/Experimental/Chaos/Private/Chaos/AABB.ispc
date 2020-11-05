// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Transform.isph"

inline static void GrowToInclude(uniform FVector4 &Min, uniform FVector4 &Max, const uniform FVector4& V)
{
	Min = VectorMin(Min, V);
	Max = VectorMax(Max, V);
}

inline static void GrowToInclude(uniform FVector8 &MinMax, const uniform FVector8& V)
{
	const uniform FVector8 VReverse = SetVector8(V.V[4], V.V[5], V.V[6], V.V[7], V.V[0], V.V[1], V.V[2], V.V[3]);

	const uniform FVector8 Min = VectorMin(VectorMin(MinMax, V), VReverse);
	const uniform FVector8 Max = VectorMax(VectorMax(MinMax, V), VReverse);

	MinMax = SetVector8(Min.V[0], Min.V[1], Min.V[2], Min.V[3], Max.V[4], Max.V[5], Max.V[6], Max.V[7]);
}

export void TransformedAABB(const uniform FTransform &SpaceTransform, const uniform FVector &MMin, const uniform FVector &MMax, uniform FVector &NewMin, uniform FVector &NewMax)
{
	const uniform FVector4 Min = SetVector4(MMin.V[0], MMin.V[1], MMin.V[2], 0.0f);
	const uniform FVector4 Max = SetVector4(MMax.V[0], MMax.V[1], MMax.V[2], 0.0f);
	const uniform FVector4 CurrentExtents = Max - Min;

#if TARGET_WIDTH == 4
	uniform FVector4 MinToNewSpace = TransformPosition(SpaceTransform, Min);
	uniform FVector4 MaxToNewSpace = MinToNewSpace;
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max));

	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Min + ForwardVector4 * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max - ForwardVector4 * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Min + RightVector4 * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max - RightVector4 * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Min + UpVector4 * CurrentExtents));
	GrowToInclude(MinToNewSpace, MaxToNewSpace, TransformPosition(SpaceTransform, Max - UpVector4 * CurrentExtents));

	NewMin = SetVector(MinToNewSpace);
	NewMax = SetVector(MaxToNewSpace);
#else
	const uniform FVector8 MinMax = SetVector8(Min, Max);
	const uniform FVector8 CurrentExtentsWide = SetVector8(CurrentExtents);
	const uniform FTransform8 SpaceTransformWide = SetTransform8(SpaceTransform);
	const uniform FVector8 MinMaxTransform = TransformPosition(SpaceTransformWide, MinMax);
	const uniform FVector8 ForwardVectorTransform = TransformPosition(SpaceTransformWide, MinMax + ForwardVector8 * CurrentExtentsWide);
	const uniform FVector8 RightVectorTransform = TransformPosition(SpaceTransformWide, MinMax + RightVector8 * CurrentExtentsWide);
	const uniform FVector8 UpVectorTransform = TransformPosition(SpaceTransformWide, MinMax + UpVector8 * CurrentExtentsWide);

	uniform FVector8 MinMaxToNewSpace = MinMaxTransform;
	GrowToInclude(MinMaxToNewSpace, MinMaxTransform);
	GrowToInclude(MinMaxToNewSpace, ForwardVectorTransform);
	GrowToInclude(MinMaxToNewSpace, RightVectorTransform);
	GrowToInclude(MinMaxToNewSpace, UpVectorTransform);

	NewMin = SetVector(MinMaxToNewSpace.V[0], MinMaxToNewSpace.V[1], MinMaxToNewSpace.V[2]);
	NewMax = SetVector(MinMaxToNewSpace.V[4], MinMaxToNewSpace.V[5], MinMaxToNewSpace.V[6]);
#endif
}
