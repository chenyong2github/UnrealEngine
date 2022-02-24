// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSoftsEvolutionFwd.h"
#include "Chaos/Array.h"
#include "Chaos/PBDSoftsSolverParticles.h"
#include "Chaos/ParticleRule.h"
#include "GenericPlatform/GenericPlatformMath.h"

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
			MAngles.Add(GetAngle(P1, P2, P3, P4));
		}
	}
	virtual ~FPBDBendingConstraintsBase() {}

	TArray<FSolverVec3> GetGradients(const FSolverParticles& InParticles, const int32 i) const
	{
		TArray<FSolverVec3> Grads;
		Grads.SetNum(4);
		const auto& Constraint = Constraints[i];
		const FSolverVec3& P1 = InParticles.P(Constraint[0]);
		const FSolverVec3& P2 = InParticles.P(Constraint[1]);
		const FSolverVec3& P3 = InParticles.P(Constraint[2]);
		const FSolverVec3& P4 = InParticles.P(Constraint[3]);
		const FSolverVec3 Edge = P2 - P1;
		auto Normal1 = FSolverVec3::CrossProduct(P3 - P1, P3 - P2);
		SafeDivide(Normal1, Normal1.SizeSquared());
		auto Normal2 = FSolverVec3::CrossProduct(P4 - P2, P4 - P1);
		SafeDivide(Normal2, Normal2.SizeSquared());
		FSolverReal EdgeSize = Edge.Size();
		Grads[0] = SafeDivide(FSolverVec3::DotProduct(Edge, P3 - P2), EdgeSize) * Normal1 + SafeDivide(FSolverVec3::DotProduct(Edge, P4 - P2), EdgeSize) * Normal2;
		Grads[1] = SafeDivide(FSolverVec3::DotProduct(Edge, P1 - P3), EdgeSize) * Normal1 + SafeDivide(FSolverVec3::DotProduct(Edge, P1 - P4), EdgeSize) * Normal2;
		Grads[2] = EdgeSize * Normal1;
		Grads[3] = EdgeSize * Normal2;
		return Grads;
	}

	FSolverReal GetScalingFactor(const FSolverParticles& InParticles, const int32 i, const TArray<FSolverVec3>& Grads) const
	{
		const auto& Constraint = Constraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const FSolverVec3& P1 = InParticles.P(i1);
		const FSolverVec3& P2 = InParticles.P(i2);
		const FSolverVec3& P3 = InParticles.P(i3);
		const FSolverVec3& P4 = InParticles.P(i4);
		FSolverReal Angle = GetAngle(P1, P2, P3, P4);
		FSolverReal Denom = (InParticles.InvM(i1) * Grads[0].SizeSquared() + InParticles.InvM(i2) * Grads[1].SizeSquared() + InParticles.InvM(i3) * Grads[2].SizeSquared() + InParticles.InvM(i4) * Grads[3].SizeSquared());
		{
			auto Edge = P2 - P1;
			auto Normal1 = FSolverVec3::CrossProduct(P3 - P1, P3 - P2).GetSafeNormal();
			auto Normal2 = FSolverVec3::CrossProduct(P4 - P2, P4 - P1).GetSafeNormal();
			Denom = FSolverVec3::DotProduct(Edge, FSolverVec3::CrossProduct(Normal1, Normal2)) > (FSolverReal)0. ? -Denom : Denom;
		}
		FSolverReal Delta = Angle - MAngles[i];
		return Stiffness * SafeDivide(Delta, Denom);
	}

	void SetStiffness(FSolverReal InStiffness) { Stiffness = FMath::Clamp(InStiffness, (FSolverReal)0., (FSolverReal)1.); }

  private:
	template<class TNum>
	inline TNum SafeDivide(const TNum& Numerator, const FSolverReal& Denominator) const
	{
		if (Denominator > (FSolverReal)1e-7)
			return Numerator / Denominator;
		return TNum(0);
	}

	inline FSolverReal Clamp(const FSolverReal& Value, const FSolverReal& Low, const FSolverReal& High) const
	{
		return Value < Low ? Low : (Value > High ? High : Value);
	}

	FSolverReal GetAngle(const FSolverVec3& P1, const FSolverVec3& P2, const FSolverVec3& P3, const FSolverVec3& P4) const
	{
		auto Normal1 = FSolverVec3::CrossProduct(P3 - P1, P3 - P2).GetSafeNormal();
		auto Normal2 = FSolverVec3::CrossProduct(P4 - P2, P4 - P1).GetSafeNormal();
		auto Dot = FSolverVec3::DotProduct(Normal1, Normal2);
		return FGenericPlatformMath::Acos(Clamp(Dot, 1e-4, 1 - 1e-4));
	}

  protected:
	TArray<TVec4<int32>> Constraints;

  private:
	TArray<FSolverReal> MAngles;
	FSolverReal Stiffness;
};

}  // End namespace Chaos::Softs
