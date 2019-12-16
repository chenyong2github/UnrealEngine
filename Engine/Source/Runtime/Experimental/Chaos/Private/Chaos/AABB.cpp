// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/AABB.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#if INTEL_ISPC
#include "AABB.ispc.generated.h"
#endif

namespace Chaos
{
template <typename T, int d>
bool TAABB<T, d>::Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const
{
	ensure(Length > 0);
	ensure(FMath::IsNearlyEqual(Dir.SizeSquared(), 1, KINDA_SMALL_NUMBER));

	OutFaceIndex = INDEX_NONE;
	const TVector<T, d> MinInflated = MMin - Thickness;
	const TVector<T,d> StartToMin = MinInflated - StartPoint;
	
	const TVector<T, d> MaxInflated = MMax + Thickness;
	const TVector<T, d> StartToMax = MaxInflated - StartPoint;

	//For each axis record the start and end time when ray is in the box. If the intervals overlap the ray is inside the box
	T LatestStartTime = 0;
	T EarliestEndTime = FLT_MAX;
	TVector<T, d> Normal(0);	//not needed but fixes compiler warning

	for (int Axis = 0; Axis < d; ++Axis)
	{
		const bool bParallel = FMath::IsNearlyZero(Dir[Axis]);
		T Time1, Time2;
		if (bParallel)
		{
			if (StartToMin[Axis] > 0 || StartToMax[Axis] < 0)
			{
				return false;	//parallel and outside
			}
			else
			{
				Time1 = 0;
				Time2 = FLT_MAX;
			}
		}
		else
		{
			const T InvDir = (T)1 / Dir[Axis];
			Time1 = StartToMin[Axis] * InvDir;
			Time2 = StartToMax[Axis] * InvDir;
		}

		TVector<T, d> CurNormal = TVector<T, d>::AxisVector(Axis);

		if (Time1 > Time2)
		{
			//going from max to min direction
			std::swap(Time1, Time2);
		}
		else
		{
			//hit negative plane first
			CurNormal[Axis] = -1;
		}

		if (Time1 > LatestStartTime)
		{
			//last plane to enter so save its normal
			Normal = CurNormal;
		}
		LatestStartTime = FMath::Max(LatestStartTime, Time1);
		EarliestEndTime = FMath::Min(EarliestEndTime, Time2);

		if (LatestStartTime > EarliestEndTime)
		{
			return false;	//Outside of slab before entering another
		}
	}

	//infinite ray intersects with inflated box
	if (LatestStartTime > Length || EarliestEndTime < 0)
	{
		//outside of line segment given
		return false;
	}
	
	const TVector<T, d> BoxIntersection = StartPoint + LatestStartTime * Dir;

	//If the box is rounded we have to consider corners and edges.
	//Break the box into voronoi regions based on features (corner, edge, face) and see which region the raycast hit

	if (Thickness)
	{
		check(d == 3);
		TVector<T, d> GeomStart;
		TVector<T, d> GeomEnd;
		int32 NumAxes = 0;

		for (int Axis = 0; Axis < d; ++Axis)
		{
			if (BoxIntersection[Axis] < MMin[Axis])
			{
				GeomStart[Axis] = MMin[Axis];
				GeomEnd[Axis] = MMin[Axis];
				++NumAxes;
			}
			else if (BoxIntersection[Axis] > MMax[Axis])
			{
				GeomStart[Axis] = MMax[Axis];
				GeomEnd[Axis] = MMax[Axis];
				++NumAxes;
			}
			else
			{
				GeomStart[Axis] = MMin[Axis];
				GeomEnd[Axis] = MMax[Axis];
			}
		}

		if (NumAxes >= 2)
		{
			bool bHit = false;
			if (NumAxes == 3)
			{
				//hit a corner. For now just use 3 capsules, there's likely a better way to determine which capsule is needed
				T CornerTimes[3];
				TVector<T, d> CornerPositions[3];
				TVector<T, d> CornerNormals[3];
				int32 HitIdx = INDEX_NONE;
				T MinTime = 0;	//initialization just here for compiler warning
				for (int CurIdx = 0; CurIdx < 3; ++CurIdx)
				{
					TVector<T, d> End = GeomStart;
					End[CurIdx] = End[CurIdx] == MMin[CurIdx] ? MMax[CurIdx] : MMin[CurIdx];
					TCapsule<T> Capsule(GeomStart, End, Thickness);
					if (Capsule.Raycast(StartPoint, Dir, Length, 0, CornerTimes[CurIdx], CornerPositions[CurIdx], CornerNormals[CurIdx], OutFaceIndex))
					{
						if (HitIdx == INDEX_NONE || CornerTimes[CurIdx] < MinTime)
						{
							MinTime = CornerTimes[CurIdx];
							HitIdx = CurIdx;

							if (MinTime == 0)
							{
								OutTime = 0;	//initial overlap so just exit
								return true;
							}
						}
					}
				}

				if (HitIdx != INDEX_NONE)
				{
					OutPosition = CornerPositions[HitIdx];
					OutTime = MinTime;
					OutNormal = CornerNormals[HitIdx];
					bHit = true;
				}
			}
			else
			{
				//capsule: todo(use a cylinder which is cheaper. Our current cylinder raycast implementation doesn't quite work for this setup)
				TCapsule<T> CapsuleBorder(GeomStart, GeomEnd, Thickness);
				bHit = CapsuleBorder.Raycast(StartPoint, Dir, Length, 0, OutTime, OutPosition, OutNormal, OutFaceIndex);
			}

			if (bHit && OutTime > 0)
			{
				OutPosition -= OutNormal * Thickness;
			}
			return bHit;
		}
	}
	
	// didn't hit any rounded parts so just use the box intersection
	OutTime = LatestStartTime;
	OutNormal = Normal;
	OutPosition = BoxIntersection - Thickness * Normal;
	return true;
}

template<typename T, int d>
template<class TTRANSFORM>
TAABB<T, d> TAABB<T, d>::TransformedAABB(const TTRANSFORM& SpaceTransform) const
{
	TVector<T, d> CurrentExtents = Extents();
	int32 Idx = 0;
	const TVector<T, d> MinToNewSpace = SpaceTransform.TransformPosition(MMin);
	TAABB<T, d> NewAABB(MinToNewSpace, MinToNewSpace);
	NewAABB.GrowToInclude(SpaceTransform.TransformPosition(MMax));

	for (int32 j = 0; j < d; ++j)
	{
		NewAABB.GrowToInclude(SpaceTransform.TransformPosition(MMin + TVector<T, d>::AxisVector(j) * CurrentExtents));
		NewAABB.GrowToInclude(SpaceTransform.TransformPosition(MMax - TVector<T, d>::AxisVector(j) * CurrentExtents));
	}

	return NewAABB;
}

template<>
template<>
TAABB<float, 3> TAABB<float, 3>::TransformedAABB(const FTransform& SpaceTransform) const
{
	if (INTEL_ISPC)
	{
#if INTEL_ISPC
		TVector<float, 3> NewMin, NewMax;
		ispc::TransformedAABB((const ispc::FTransform&)SpaceTransform, (const ispc::FVector&)MMin, (const ispc::FVector&)MMax, (ispc::FVector&)NewMin, (ispc::FVector&)NewMax);

		TAABB<float, 3> NewAABB(NewMin, NewMax);
		return NewAABB;
#endif
	}
	else
	{
		TVector<float, 3> CurrentExtents = Extents();
		int32 Idx = 0;
		const TVector<float, 3> MinToNewSpace = SpaceTransform.TransformPosition(MMin);
		TAABB<float, 3> NewAABB(MinToNewSpace, MinToNewSpace);
		NewAABB.GrowToInclude(SpaceTransform.TransformPosition(MMax));

		for (int32 j = 0; j < 3; ++j)
		{
			NewAABB.GrowToInclude(SpaceTransform.TransformPosition(MMin + TVector<float, 3>::AxisVector(j) * CurrentExtents));
			NewAABB.GrowToInclude(SpaceTransform.TransformPosition(MMax - TVector<float, 3>::AxisVector(j) * CurrentExtents));
		}

		return NewAABB;
	}
}
}

template class Chaos::TAABB<float, 3>;

template Chaos::TAABB<float, 3> Chaos::TAABB<float, 3>::TransformedAABB(const Chaos::TRigidTransform<float, 3>&) const;
template Chaos::TAABB<float, 3> Chaos::TAABB<float, 3>::TransformedAABB(const FMatrix&) const;
template Chaos::TAABB<float, 3> Chaos::TAABB<float, 3>::TransformedAABB(const Chaos::PMatrix<float, 4, 4>&) const;
