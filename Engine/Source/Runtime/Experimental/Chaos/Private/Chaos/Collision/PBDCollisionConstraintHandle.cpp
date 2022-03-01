// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Evolution/SolverBody.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/PBDCollisionConstraints.h"

namespace Chaos
{
	const FPBDCollisionConstraints* FPBDCollisionConstraintHandle::ConcreteContainer() const
	{
		return static_cast<FPBDCollisionConstraints*>(ConstraintContainer);
	}

	FPBDCollisionConstraints* FPBDCollisionConstraintHandle::ConcreteContainer()
	{
		return static_cast<FPBDCollisionConstraints*>(ConstraintContainer);
	}

	const FPBDCollisionConstraint& FPBDCollisionConstraintHandle::GetContact() const
	{
		return *GetConstraint();
	}

	FPBDCollisionConstraint& FPBDCollisionConstraintHandle::GetContact()
	{
		return *GetConstraint();
	}

	ECollisionCCDType FPBDCollisionConstraintHandle::GetCCDType() const
	{
		return GetContact().GetCCDType();
	}

	void FPBDCollisionConstraintHandle::SetEnabled(bool InEnabled)
	{
		GetContact().SetDisabled(!InEnabled);
	}

	bool FPBDCollisionConstraintHandle::IsEnabled() const
	{
		return !GetContact().GetDisabled();
	}

	FVec3 FPBDCollisionConstraintHandle::GetAccumulatedImpulse() const
	{
		return GetContact().AccumulatedImpulse;
	}

	TVector<const TGeometryParticleHandle<FReal, 3>*, 2> FPBDCollisionConstraintHandle::GetConstrainedParticles() const
	{
		return { GetContact().GetParticle0(), GetContact().GetParticle1() };
	}

	TVector<TGeometryParticleHandle<FReal, 3>*, 2> FPBDCollisionConstraintHandle::GetConstrainedParticles()
	{
		return { GetContact().GetParticle0(), GetContact().GetParticle1() };
	}

	void FPBDCollisionConstraintHandle::PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData)
	{
		ConcreteContainer()->PreGatherInput(GetContact(), SolverData);
	}

	void FPBDCollisionConstraintHandle::GatherInput(FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
	{
		ConcreteContainer()->GatherInput(Dt, GetContact(), Particle0Level, Particle1Level, SolverData);
	}

	FSolverBody* FPBDCollisionConstraintHandle::GetSolverBody0()
	{
		return GetContact().GetSolverBody0();
	}

	FSolverBody* FPBDCollisionConstraintHandle::GetSolverBody1()
	{
		return GetContact().GetSolverBody1();
	}
}