// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSoftsEvolutionFwd.h"
#include "Chaos/PBDSoftsSolverParticles.h"
#include "Chaos/ParticleRule.h"
#include "Containers/StaticArray.h"

namespace Chaos::Softs
{

class FPBDBendingConstraintsBase
{
  public:
	  FPBDBendingConstraintsBase(const FSolverParticles& InParticles, TArray<TVec4<int32>>&& InConstraints, const FSolverReal InStiffness = (FSolverReal)1.)
	    : Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
	{
		for (const TVec4<int32>& Constraint : Constraints)
		{
			const FSolverVec3& P1 = InParticles.X(Constraint[0]);
			const FSolverVec3& P2 = InParticles.X(Constraint[1]);
			const FSolverVec3& P3 = InParticles.X(Constraint[2]);
			const FSolverVec3& P4 = InParticles.X(Constraint[3]);
			MAngles.Add(CalcAngle(P1, P2, P3, P4));
		}
	}
	virtual ~FPBDBendingConstraintsBase() {}

	TStaticArray<FSolverVec3, 4> GetGradients(const FSolverParticles& InParticles, const int32 i) const
	{
		const TVec4<int32>& Constraint = Constraints[i];
		const FSolverVec3& P1 = InParticles.P(Constraint[0]);
		const FSolverVec3& P2 = InParticles.P(Constraint[1]);
		const FSolverVec3& P3 = InParticles.P(Constraint[2]);
		const FSolverVec3& P4 = InParticles.P(Constraint[3]);

		return CalcGradients(P1, P2, P3, P4);
	}

	FSolverReal GetScalingFactor(const FSolverParticles& InParticles, const int32 i, const TStaticArray<FSolverVec3, 4>& Grads) const
	{
		const TVec4<int32>& Constraint = Constraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const FSolverVec3& P1 = InParticles.P(i1);
		const FSolverVec3& P2 = InParticles.P(i2);
		const FSolverVec3& P3 = InParticles.P(i3);
		const FSolverVec3& P4 = InParticles.P(i4);
		const FSolverReal Angle = CalcAngle(P1, P2, P3, P4);
		const FSolverReal Denom = (InParticles.InvM(i1) * Grads[0].SizeSquared() + InParticles.InvM(i2) * Grads[1].SizeSquared() + InParticles.InvM(i3) * Grads[2].SizeSquared() + InParticles.InvM(i4) * Grads[3].SizeSquared());
		constexpr FSolverReal SingleStepAngleLimit = (FSolverReal)(UE_PI * .25f); // this constraint is very non-linear. taking large steps is not accurate
		const FSolverReal Delta = FMath::Clamp(Stiffness*(Angle - MAngles[i]), -SingleStepAngleLimit, SingleStepAngleLimit);
		return SafeDivide(Delta, Denom);
	}

	void SetStiffness(FSolverReal InStiffness) { Stiffness = FMath::Clamp(InStiffness, (FSolverReal)0., (FSolverReal)1.); }

  private:
	template<class TNum>
	static inline TNum SafeDivide(const TNum& Numerator, const FSolverReal& Denominator)
	{
		if (Denominator > SMALL_NUMBER)
			return Numerator / Denominator;
		return TNum(0);
	}

	static FSolverReal CalcAngle(const FSolverVec3& P1, const FSolverVec3& P2, const FSolverVec3& P3, const FSolverVec3& P4)
	{
		const FSolverVec3 Normal1 = FSolverVec3::CrossProduct(P1 - P3, P2 - P3).GetSafeNormal();
		const FSolverVec3 Normal2 = FSolverVec3::CrossProduct(P2 - P4, P1 - P4).GetSafeNormal();

		const FSolverVec3 SharedEdge = (P2 - P1).GetSafeNormal();

		const FSolverReal CosPhi = FMath::Clamp(FSolverVec3::DotProduct(Normal1, Normal2), (FSolverReal)-1, (FSolverReal)1);
		const FSolverReal SinPhi = FMath::Clamp(FSolverVec3::DotProduct(FSolverVec3::CrossProduct(Normal2, Normal1), SharedEdge), (FSolverReal)-1, (FSolverReal)1);
		return FMath::Atan2(SinPhi, CosPhi);
	}
	
	static TStaticArray<FSolverVec3, 4> CalcGradients(const FSolverVec3& P1, const FSolverVec3& P2, const FSolverVec3& P3, const FSolverVec3& P4)
	{
		TStaticArray<FSolverVec3, 4> Grads;
		// Calculated using Phi = atan2(SinPhi, CosPhi)
		// where SinPhi = (Normal1 ^ Normal2)*SharedEdgeNormalized, CosPhi = Normal1 * Normal2
		// Full gradients are calculated here, i.e., no simplifying assumptions around things like edge lengths being constant.
		const FSolverVec3 SharedEdgeNormalized = (P2 - P1).GetSafeNormal();
		const FSolverVec3 P13CrossP23 = FSolverVec3::CrossProduct(P1 - P3, P2 - P3);
		const FSolverReal Normal1Len = P13CrossP23.Size();
		const FSolverVec3 Normal1 = SafeDivide(P13CrossP23, Normal1Len);
		const FSolverVec3 P24CrossP14 = FSolverVec3::CrossProduct(P2 - P4, P1 - P4);
		const FSolverReal Normal2Len = P24CrossP14.Size();
		const FSolverVec3 Normal2 = SafeDivide(P24CrossP14, Normal2Len);

		const FSolverVec3 N2CrossN1 = FSolverVec3::CrossProduct(Normal2, Normal1);

		const FSolverReal CosPhi = FMath::Clamp(FSolverVec3::DotProduct(Normal1, Normal2), (FSolverReal)-1, (FSolverReal)1);
		const FSolverReal SinPhi = FMath::Clamp(FSolverVec3::DotProduct(N2CrossN1, SharedEdgeNormalized), (FSolverReal)-1, (FSolverReal)1);

		const FSolverVec3 DPhiDN1_OverNormal1Len = SafeDivide(CosPhi * FSolverVec3::CrossProduct(SharedEdgeNormalized, Normal2) - SinPhi * Normal2, Normal1Len);
		const FSolverVec3 DPhiDN2_OverNormal2Len = SafeDivide(CosPhi * FSolverVec3::CrossProduct(Normal1, SharedEdgeNormalized) - SinPhi * Normal1, Normal2Len);

		const FSolverVec3 DPhiDP13 = FSolverVec3::CrossProduct(P2 - P3, DPhiDN1_OverNormal1Len);
		const FSolverVec3 DPhiDP23 = FSolverVec3::CrossProduct(DPhiDN1_OverNormal1Len, P1 - P3);
		const FSolverVec3 DPhiDP24 = FSolverVec3::CrossProduct(P1 - P4, DPhiDN2_OverNormal2Len);
		const FSolverVec3 DPhiDP14 = FSolverVec3::CrossProduct(DPhiDN2_OverNormal2Len, P2 - P4);

		Grads[0] = DPhiDP13 + DPhiDP14;
		Grads[1] = DPhiDP23 + DPhiDP24;
		Grads[2] = -DPhiDP13 - DPhiDP23;
		Grads[3] = -DPhiDP14 - DPhiDP24;

		return Grads;
	}

  protected:
	TArray<TVec4<int32>> Constraints;

  private:
	TArray<FSolverReal> MAngles;
	FSolverReal Stiffness;
};

}  // End namespace Chaos::Softs
