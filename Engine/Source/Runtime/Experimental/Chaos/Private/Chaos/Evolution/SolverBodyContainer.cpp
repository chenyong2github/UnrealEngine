// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/Particle/ParticleUtilities.h"

namespace Chaos
{
	void FSolverBodyAdapter::ScatterOutput()
	{
		if (Particle.IsValid())
		{
			if (SolverBody.IsDynamic())
			{
				// Set the particle state
				FParticleUtilities::SetCoMWorldTransform(Particle, SolverBody.CorrectedP(), SolverBody.CorrectedQ());
				Particle->SetV(SolverBody.V());
				Particle->SetW(SolverBody.W());
			}

			// Reset SolverBodyIndex cookie every step - it will be reassigned next step
			Particle->SetSolverBodyIndex(INDEX_NONE);
		}
	}

	int32 FSolverBodyContainer::AddParticle(FGenericParticleHandle InParticle)
	{
		// No array resizing allowed (we want fixed pointers)
		check(NumItems() < MaxItems());
		return SolverBodies.Emplace(InParticle);
	}

	FSolverBody* FSolverBodyContainer::FindOrAdd(FGenericParticleHandle InParticle)
	{
		// For dynamic bodies, we store a cookie on the Particle that holds the solver body index
		// For kinematics we cannot do this because the kinematic may be in multiple islands and 
		// would require a different index for each island, so we use a local map instead. 
		int32 ItemIndex = InParticle->SolverBodyIndex();
	
		if (ItemIndex == INDEX_NONE)
		{
			if (InParticle->IsDynamic())
			{
				// First time we have seen this particle, so add it
				ItemIndex = AddParticle(InParticle);
				InParticle->SetSolverBodyIndex(ItemIndex);
			}
			else // Not Dynamic
			{
				int32* ItemIndexPtr = ParticleToIndexMap.Find(InParticle);
				if (ItemIndexPtr != nullptr)
				{
					ItemIndex = *ItemIndexPtr;
				}
				else
				{
					// First time we have seen this particle, so add it
					ItemIndex = AddParticle(InParticle);
					ParticleToIndexMap.Add(InParticle, ItemIndex);
				}
			}			
		}
	
		check(ItemIndex != INDEX_NONE);
		return &SolverBodies[ItemIndex].GetSolverBody();
	}

	void FSolverBodyContainer::ScatterOutput()
	{
		for (FSolverBodyAdapter& SolverBody : SolverBodies)
		{
			SolverBody.ScatterOutput();
		}
	}

	void FSolverBodyContainer::SetImplicitVelocities(FReal Dt)
	{
		for (FSolverBodyAdapter& SolverBody : SolverBodies)
		{
			SolverBody.GetSolverBody().SetImplicitVelocity(Dt);
		}
	}

	void FSolverBodyContainer::ApplyCorrections()
	{
		for (FSolverBodyAdapter& SolverBody : SolverBodies)
		{
			SolverBody.GetSolverBody().ApplyCorrections();
		}
	}

	void FSolverBodyContainer::UpdateRotationDependentState()
	{
		for (FSolverBodyAdapter& SolverBody : SolverBodies)
		{
			SolverBody.GetSolverBody().UpdateRotationDependentState();
		}
	}

}