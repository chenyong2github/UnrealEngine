// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidDynamicSpringConstraintsBase.h"

using namespace Chaos;

template<class T, int d>
void TPBDRigidDynamicSpringConstraintsBase<T, d>::UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles)
{
	const int32 NumConstraints = Constraints.Num();
	for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints; ++ConstraintIndex)
	{
		int32 ConstraintInnerIndex1 = Constraints[ConstraintIndex][0];
		int32 ConstraintInnerIndex2 = Constraints[ConstraintIndex][1];

		// Do not create springs between objects with no geometry
		if (!InParticles.Geometry(ConstraintInnerIndex1) || !InParticles.Geometry(ConstraintInnerIndex2))
		{
			continue;
		}

		// Delete constraints
		const int32 NumSprings = SpringDistances[ConstraintIndex].Num();
		for (int32 SpringIndex = NumSprings - 1; SpringIndex >= 0; --SpringIndex)
		{
			const TVector<T, d> WorldSpaceX1 = InParticles.Q(ConstraintInnerIndex1).RotateVector(Distances[ConstraintIndex][SpringIndex][0]) + InParticles.P(ConstraintInnerIndex1);
			const TVector<T, d> WorldSpaceX2 = InParticles.Q(ConstraintInnerIndex2).RotateVector(Distances[ConstraintIndex][SpringIndex][1]) + InParticles.P(ConstraintInnerIndex2);
			const TVector<T, d> Difference = WorldSpaceX2 - WorldSpaceX1;
			float Distance = Difference.Size();
			if (Distance > CreationThreshold * 2)
			{
				Distances[ConstraintIndex].RemoveAtSwap(SpringIndex);
				SpringDistances[ConstraintIndex].RemoveAtSwap(SpringIndex);
			}
		}

		if (SpringDistances[ConstraintIndex].Num() == MaxSprings)
		{
			continue;
		}

		TRigidTransform<T, d> Transform1(InParticles.P(ConstraintInnerIndex1), InParticles.Q(ConstraintInnerIndex1));
		TRigidTransform<T, d> Transform2(InParticles.P(ConstraintInnerIndex2), InParticles.Q(ConstraintInnerIndex2));

		// Create constraints
		if (InParticles.Geometry(ConstraintInnerIndex1)->HasBoundingBox() && InParticles.Geometry(ConstraintInnerIndex2)->HasBoundingBox())
		{
			// Matrix multiplication is reversed intentionally to be compatible with unreal
			TBox<T, d> Box1 = InParticles.Geometry(ConstraintInnerIndex1)->BoundingBox().TransformedBox(Transform1 * Transform2.Inverse());
			Box1.Thicken(CreationThreshold);
			TBox<T, d> Box2 = InParticles.Geometry(ConstraintInnerIndex2)->BoundingBox();
			Box2.Thicken(CreationThreshold);
			if (!Box1.Intersects(Box2))
			{
				continue;
			}
		}
		const TVector<T, d> Midpoint = (InParticles.P(ConstraintInnerIndex1) + InParticles.P(ConstraintInnerIndex2)) / (T)2;
		TVector<T, d> Normal1;
		const T Phi1 = InParticles.Geometry(ConstraintInnerIndex1)->PhiWithNormal(Transform1.InverseTransformPosition(Midpoint), Normal1);
		Normal1 = Transform2.TransformVector(Normal1);
		TVector<T, d> Normal2;
		const T Phi2 = InParticles.Geometry(ConstraintInnerIndex2)->PhiWithNormal(Transform2.InverseTransformPosition(Midpoint), Normal2);
		Normal2 = Transform2.TransformVector(Normal2);
		if ((Phi1 + Phi2) > CreationThreshold)
		{
			continue;
		}
		TVector<T, d> Location0 = Midpoint - Phi1 * Normal1;
		TVector<T, d> Location1 = Midpoint - Phi2 * Normal2;
		TVector<TVector<T, 3>, 2> Distance;
		Distance[0] = InParticles.Q(ConstraintInnerIndex1).Inverse().RotateVector(Location0 - InParticles.P(ConstraintInnerIndex1));
		Distance[1] = InParticles.Q(ConstraintInnerIndex2).Inverse().RotateVector(Location1 - InParticles.P(ConstraintInnerIndex2));
		Distances[ConstraintIndex].Add(MoveTemp(Distance));
		SpringDistances[ConstraintIndex].Add((Location0 - Location1).Size());
	}
}

template<class T, int d>
TVector<T, d> TPBDRigidDynamicSpringConstraintsBase<T, d>::GetDelta(const TPBDRigidParticles<T, d>& InParticles, const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const int32 i, const int32 j) const
{
	const TVector<int32, 2>& Constraint = Constraints[i];

	int32 i1 = Constraint[0];
	int32 i2 = Constraint[1];

	if (InParticles.InvM(i2) == 0 && InParticles.InvM(i1) == 0)
	{
		return TVector<T, d>(0);
	}

	TVector<T, d> Difference = WorldSpaceX2 - WorldSpaceX1;
	float Distance = Difference.Size();

	check(Distance > 1e-7);

	TVector<T, d> Direction = Difference / Distance;
	TVector<T, d> Delta = (Distance - SpringDistances[i][j]) * Direction;
	T CombinedMass = InParticles.InvM(i2) + InParticles.InvM(i1);
	return Stiffness * Delta / CombinedMass;
}

namespace Chaos
{
	template class TPBDRigidDynamicSpringConstraintsBase<float, 3>;
}
