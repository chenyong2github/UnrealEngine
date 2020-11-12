// Copyright Epic Games, Inc. All Rights Reserved.

#include "PBDRigidActiveParticlesBuffer.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace Chaos
{

	FPBDRigidDirtyParticlesBuffer::FPBDRigidDirtyParticlesBuffer(const Chaos::EMultiBufferMode& InBufferMode, bool bInSingleThreaded) : BufferMode(InBufferMode), bUseLock(!bInSingleThreaded)
	{
		SolverDataOut = Chaos::FMultiBufferFactory<FPBDRigidDirtyParticlesBufferOut>::CreateBuffer(InBufferMode);
	}


	template <typename Traits>
	void FPBDRigidDirtyParticlesBuffer::CaptureSolverData(TPBDRigidsSolver<Traits>* Solver)
	{
		WriteLock();
		BufferPhysicsResults(Solver);
		FlipDataOut();
		WriteUnlock();
	}

	template <typename Traits>
	void FPBDRigidDirtyParticlesBuffer::BufferPhysicsResults(TPBDRigidsSolver<Traits>* Solver)
	{
		auto& ActiveGameThreadParticles = SolverDataOut->AccessProducerBuffer()->DirtyGameThreadParticles;
		auto& PhysicsParticleProxies = SolverDataOut->AccessProducerBuffer()->PhysicsParticleProxies;

		ActiveGameThreadParticles.Empty();
		PhysicsParticleProxies.Empty();
		TParticleView<TPBDRigidParticles<float, 3>>& ActiveParticlesView = Solver->GetParticles().GetDirtyParticlesView();
		for (auto& ActiveParticle : ActiveParticlesView)
		{
			if (ActiveParticle.Handle())	//can this be null?
			{
				if (const TSet<IPhysicsProxyBase*>* Proxies = Solver->GetProxies(ActiveParticle.Handle()))//can this be null?
				{
					for(IPhysicsProxyBase* Proxy : *Proxies)
					{
						if(Proxy != nullptr)	//can this be null?
						{
							if(Proxy->GetType() == EPhysicsProxyType::SingleRigidParticleType)
							{
								ensure(Proxies->Num() == 1);	//single rigid should only have one proxy
								ActiveGameThreadParticles.Add(static_cast<FSingleParticlePhysicsProxy<TPBDRigidParticle<float,3> >*>(Proxy));
							}
							else
							{
								//must be a geometry collection
								PhysicsParticleProxies.Add(Proxy);
							}
						}
					}
				}
			}
		}
	}

	void FPBDRigidDirtyParticlesBuffer::ReadLock()
	{
		if (bUseLock)
		{
			ResourceOutLock.ReadLock();
		}
	}

	void FPBDRigidDirtyParticlesBuffer::ReadUnlock()
	{
		if (bUseLock)
		{
			ResourceOutLock.ReadUnlock();
		}
	}

	void FPBDRigidDirtyParticlesBuffer::WriteLock()
	{
		if (bUseLock)
		{
			ResourceOutLock.WriteLock();
		}
	}

	void FPBDRigidDirtyParticlesBuffer::WriteUnlock()
	{
		if (bUseLock)
		{
			ResourceOutLock.WriteUnlock();
		}
	}

#define EVOLUTION_TRAIT(Trait) template void Chaos::FPBDRigidDirtyParticlesBuffer::BufferPhysicsResults<Trait>(TPBDRigidsSolver<Trait>* Solver);
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT

#define EVOLUTION_TRAIT(Trait) template void Chaos::FPBDRigidDirtyParticlesBuffer::CaptureSolverData<Trait>(TPBDRigidsSolver<Trait>* Solver);
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT

}
