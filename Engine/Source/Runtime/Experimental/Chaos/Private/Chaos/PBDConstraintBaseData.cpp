// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintBaseData.h"

namespace Chaos
{
	FConstraintBase::FConstraintBase(EConstraintType InType)
		: Type(InType)
		, Proxy(nullptr)
		, Particles({ nullptr, nullptr })
	{
	}

	bool FConstraintBase::IsValid() const
	{
		return Proxy != nullptr;
	}

	void FConstraintBase::SetProxy(IPhysicsProxyBase* InProxy)
	{
		Proxy = InProxy;
		if (Proxy)
		{
			if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
			{
				PhysicsSolverBase->AddDirtyProxy(Proxy);
			}
		}
	}

	FConstraintBase::FParticlePair FConstraintBase::GetParticles() { return Particles; }
	const FConstraintBase::FParticlePair FConstraintBase::GetParticles() const { return Particles; }
	void FConstraintBase::SetParticles(const Chaos::FConstraintBase::FParticlePair& InParticles)
	{
		Particles[0] = InParticles[0];
		Particles[1] = InParticles[1];
	}

} // Chaos
