// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PBDRigidActiveParticlesBuffer.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{

	FPBDRigidActiveParticlesBuffer::FPBDRigidActiveParticlesBuffer(const Chaos::EMultiBufferMode& InBufferMode) : BufferMode(InBufferMode)
	{
		SolverDataOut = Chaos::FMultiBufferFactory<FPBDRigidActiveParticlesBufferOut>::CreateBuffer(InBufferMode);
	}

	void FPBDRigidActiveParticlesBuffer::CaptureSolverData(FPBDRigidsSolver* Solver)
	{
		ResourceOutLock.WriteLock();
		BufferPhysicsResults(Solver);
		FlipDataOut();
		ResourceOutLock.WriteUnlock();
	}

	void FPBDRigidActiveParticlesBuffer::BufferPhysicsResults(FPBDRigidsSolver* Solver)
	{
		auto& ActiveGameThreadParticles = SolverDataOut->AccessProducerBuffer()->ActiveGameThreadParticles;

		ActiveGameThreadParticles.Empty();
		for (auto& ActiveParticle : Solver->GetParticles().GetActiveParticlesView())
		{
			if (ActiveParticle.Handle())
			{
				TGeometryParticle<float, 3>* Handle = ActiveParticle.Handle()->GTGeometryParticle();
				ActiveGameThreadParticles.Add(Handle);
			}
		}
	}

}
