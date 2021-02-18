// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/ParticleRule.h"
#include "GenericPlatform/GenericPlatformMath.h"

namespace Chaos
{
class FPBDBendingConstraintsBase
{
  public:
	  FPBDBendingConstraintsBase(const FDynamicParticles& InParticles, TArray<TVec4<int32>>&& Constraints, const FReal Stiffness = (FReal)1.)
	    : MConstraints(MoveTemp(Constraints)), MStiffness(Stiffness)
	{
		for (auto Constraint : MConstraints)
		{
			const FVec3& P1 = InParticles.X(Constraint[0]);
			const FVec3& P2 = InParticles.X(Constraint[1]);
			const FVec3& P3 = InParticles.X(Constraint[2]);
			const FVec3& P4 = InParticles.X(Constraint[3]);
			MAngles.Add(GetAngle(P1, P2, P3, P4));
		}
	}
	virtual ~FPBDBendingConstraintsBase() {}

	TArray<FVec3> GetGradients(const FPBDParticles& InParticles, const int32 i) const
	{
		TArray<FVec3> Grads;
		Grads.SetNum(4);
		const auto& Constraint = MConstraints[i];
		const FVec3& P1 = InParticles.P(Constraint[0]);
		const FVec3& P2 = InParticles.P(Constraint[1]);
		const FVec3& P3 = InParticles.P(Constraint[2]);
		const FVec3& P4 = InParticles.P(Constraint[3]);
		auto Edge = P2 - P1;
		auto Normal1 = FVec3::CrossProduct(P3 - P1, P3 - P2);
		SafeDivide(Normal1, Normal1.SizeSquared());
		auto Normal2 = FVec3::CrossProduct(P4 - P2, P4 - P1);
		SafeDivide(Normal2, Normal2.SizeSquared());
		FReal EdgeSize = Edge.Size();
		Grads[0] = SafeDivide(FVec3::DotProduct(Edge, P3 - P2), EdgeSize) * Normal1 + SafeDivide(FVec3::DotProduct(Edge, P4 - P2), EdgeSize) * Normal2;
		Grads[1] = SafeDivide(FVec3::DotProduct(Edge, P1 - P3), EdgeSize) * Normal1 + SafeDivide(FVec3::DotProduct(Edge, P1 - P4), EdgeSize) * Normal2;
		Grads[2] = EdgeSize * Normal1;
		Grads[3] = EdgeSize * Normal2;
		return Grads;
	}

	FReal GetScalingFactor(const FPBDParticles& InParticles, const int32 i, const TArray<FVec3>& Grads) const
	{
		const auto& Constraint = MConstraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const FVec3& P1 = InParticles.P(i1);
		const FVec3& P2 = InParticles.P(i2);
		const FVec3& P3 = InParticles.P(i3);
		const FVec3& P4 = InParticles.P(i4);
		FReal Angle = GetAngle(P1, P2, P3, P4);
		FReal Denom = (InParticles.InvM(i1) * Grads[0].SizeSquared() + InParticles.InvM(i2) * Grads[1].SizeSquared() + InParticles.InvM(i3) * Grads[2].SizeSquared() + InParticles.InvM(i4) * Grads[3].SizeSquared());
		{
			auto Edge = P2 - P1;
			auto Normal1 = FVec3::CrossProduct(P3 - P1, P3 - P2).GetSafeNormal();
			auto Normal2 = FVec3::CrossProduct(P4 - P2, P4 - P1).GetSafeNormal();
			Denom = FVec3::DotProduct(Edge, FVec3::CrossProduct(Normal1, Normal2)) > (FReal)0. ? -Denom : Denom;
		}
		FReal Delta = Angle - MAngles[i];
		return MStiffness * SafeDivide(Delta, Denom);
	}

	void SetStiffness(FReal InStiffness) { MStiffness = FMath::Clamp(InStiffness, (FReal)0., (FReal)1.); }

  private:
	template<class TNum>
	inline TNum SafeDivide(const TNum& Numerator, const FReal& Denominator) const
	{
		if (Denominator > (FReal)1e-7)
			return Numerator / Denominator;
		return TNum(0);
	}

	inline FReal Clamp(const FReal& Value, const FReal& Low, const FReal& High) const
	{
		return Value < Low ? Low : (Value > High ? High : Value);
	}

	FReal GetAngle(const FVec3& P1, const FVec3& P2, const FVec3& P3, const FVec3& P4) const
	{
		auto Normal1 = FVec3::CrossProduct(P3 - P1, P3 - P2).GetSafeNormal();
		auto Normal2 = FVec3::CrossProduct(P4 - P2, P4 - P1).GetSafeNormal();
		auto Dot = FVec3::DotProduct(Normal1, Normal2);
		return FGenericPlatformMath::Acos(Clamp(Dot, 1e-4, 1 - 1e-4));
	}

  protected:
	TArray<TVec4<int32>> MConstraints;

  private:
	TArray<FReal> MAngles;
	FReal MStiffness;
};
}
