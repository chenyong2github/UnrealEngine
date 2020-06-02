// Copyright Epic Games, Inc. All Rights Reserved.

#include "PBDRigidActiveParticlesBuffer.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{

	FPBDRigidDirtyParticlesBuffer::FPBDRigidDirtyParticlesBuffer(const Chaos::EMultiBufferMode& InBufferMode, bool bInSingleThreaded) : BufferMode(InBufferMode), bUseLock(!bInSingleThreaded)
	{
		SolverDataOut = Chaos::FMultiBufferFactory<FPBDRigidDirtyParticlesBufferOut>::CreateBuffer(InBufferMode);
	}

	void FPBDRigidDirtyParticlesBuffer::CaptureSolverData(FPBDRigidsSolver* Solver)
	{
		WriteLock();
		BufferPhysicsResults(Solver);
		FlipDataOut();
		WriteUnlock();
	}

	void FPBDRigidDirtyParticlesBuffer::RemoveDirtyParticleFromConsumerBuffer(TGeometryParticle<FReal, 3>* Particle)
	{
		WriteLock();
		auto& ActiveGameThreadParticles = SolverDataOut->GetConsumerBufferMutable()->DirtyGameThreadParticles;
		ActiveGameThreadParticles.RemoveSingleSwap(Particle);
		WriteUnlock();
	}

	void FPBDRigidDirtyParticlesBuffer::BufferPhysicsResults(FPBDRigidsSolver* Solver)
	{
		auto& ActiveGameThreadParticles = SolverDataOut->AccessProducerBuffer()->DirtyGameThreadParticles;
		auto& PhysicsParticleProxies = SolverDataOut->AccessProducerBuffer()->PhysicsParticleProxies;

		ActiveGameThreadParticles.Empty();
		TParticleView<TPBDRigidParticles<float, 3>>& ActiveParticlesView = 
			Solver->GetParticles().GetDirtyParticlesView();
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
}
