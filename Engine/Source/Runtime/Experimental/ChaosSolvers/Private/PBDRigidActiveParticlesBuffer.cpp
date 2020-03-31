// Copyright Epic Games, Inc. All Rights Reserved.

#include "PBDRigidActiveParticlesBuffer.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{

	FPBDRigidActiveParticlesBuffer::FPBDRigidActiveParticlesBuffer(const Chaos::EMultiBufferMode& InBufferMode, bool bInSingleThreaded) : BufferMode(InBufferMode), bUseLock(!bInSingleThreaded)
	{
		SolverDataOut = Chaos::FMultiBufferFactory<FPBDRigidActiveParticlesBufferOut>::CreateBuffer(InBufferMode);
	}

	void FPBDRigidActiveParticlesBuffer::CaptureSolverData(FPBDRigidsSolver* Solver)
	{
		WriteLock();
		BufferPhysicsResults(Solver);
		FlipDataOut();
		WriteUnlock();
	}

	void FPBDRigidActiveParticlesBuffer::RemoveActiveParticleFromConsumerBuffer(TGeometryParticle<FReal, 3>* Particle)
	{
		WriteLock();
		auto& ActiveGameThreadParticles = SolverDataOut->GetConsumerBufferMutable()->ActiveGameThreadParticles;
		ActiveGameThreadParticles.RemoveSingleSwap(Particle);
		WriteUnlock();
	}

	void FPBDRigidActiveParticlesBuffer::BufferPhysicsResults(FPBDRigidsSolver* Solver)
	{
		auto& ActiveGameThreadParticles = SolverDataOut->AccessProducerBuffer()->ActiveGameThreadParticles;
		auto& PhysicsParticleProxies = SolverDataOut->AccessProducerBuffer()->PhysicsParticleProxies;

		ActiveGameThreadParticles.Empty();
		TParticleView<TPBDRigidParticles<float, 3>>& ActiveParticlesView = 
			Solver->GetParticles().GetActiveParticlesView();
		for (auto& ActiveParticle : ActiveParticlesView)
		{
			if (ActiveParticle.Handle())
			{
				// Clustered particles don't have a game thread particle instance.
				if (TGeometryParticle<float, 3>* Handle = ActiveParticle.Handle()->GTGeometryParticle())
				{
					ActiveGameThreadParticles.Add(Handle);
				}
				else if (const TSet<IPhysicsProxyBase*> * Proxies = Solver->GetProxies(ActiveParticle.Handle()))
				{
					for (IPhysicsProxyBase* Proxy : *Proxies)
					{
						if (Proxy != nullptr)
						{
							PhysicsParticleProxies.Add(Proxy);
						}
					}
				}
			}
		}
	}

	void FPBDRigidActiveParticlesBuffer::ReadLock()
	{
		if (bUseLock)
		{
			ResourceOutLock.ReadLock();
		}
	}

	void FPBDRigidActiveParticlesBuffer::ReadUnlock()
	{
		if (bUseLock)
		{
			ResourceOutLock.ReadUnlock();
		}
	}

	void FPBDRigidActiveParticlesBuffer::WriteLock()
	{
		if (bUseLock)
		{
			ResourceOutLock.WriteLock();
		}
	}

	void FPBDRigidActiveParticlesBuffer::WriteUnlock()
	{
		if (bUseLock)
		{
			ResourceOutLock.WriteUnlock();
		}
	}
}
