// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"

namespace Chaos
{
	/**
	 * Solver specific data buffered for use on Game thread
	 */
	struct CHAOSSOLVERS_API FPBDRigidActiveParticlesBufferOut
	{
		TArray<TGeometryParticle<float, 3>*> ActiveGameThreadParticles;
		// Some particle types (clustered) only exist on the game thread, but we
		// still need to pull data over via their proxies.
		TSet<IPhysicsProxyBase*> PhysicsParticleProxies;
	};


	class CHAOSSOLVERS_API FPBDRigidActiveParticlesBuffer
	{
		friend class FPBDRigidActiveParticlesBufferAccessor;

	public:
		FPBDRigidActiveParticlesBuffer(const Chaos::EMultiBufferMode& InBufferMode, bool bInSingleThreaded);

		void CaptureSolverData(FPBDRigidsSolver* Solver);

		void RemoveActiveParticleFromConsumerBuffer(TGeometryParticle<FReal, 3>* Particle);

		void ReadLock();
		void ReadUnlock();
		void WriteLock();
		void WriteUnlock();
	
	private:
		const FPBDRigidActiveParticlesBufferOut* GetSolverOutData() const
		{
			return SolverDataOut->GetConsumerBuffer();
		}

		/**
		 * Fill data from solver destined for the game thread - used to limit the number of objects updated on the game thread
		 */
		void BufferPhysicsResults(FPBDRigidsSolver* Solver);

		/**
		 * Flip should be performed on physics thread side non-game thread
		 */
		void FlipDataOut()
		{
			SolverDataOut->FlipProducer();
		}

		Chaos::EMultiBufferMode BufferMode;
		FRWLock ResourceOutLock;
		bool bUseLock;

		// Physics thread to game thread
		TUniquePtr<IBufferResource<FPBDRigidActiveParticlesBufferOut>> SolverDataOut;
	};

	class FPBDRigidActiveParticlesBufferAccessor
	{
	public:
		FPBDRigidActiveParticlesBufferAccessor(FPBDRigidActiveParticlesBuffer* InManager) : Manager(InManager)
		{
			check(InManager);
			Manager->ReadLock();
		}

		const FPBDRigidActiveParticlesBufferOut* GetSolverOutData() const
		{
			return Manager->GetSolverOutData();
		}

		~FPBDRigidActiveParticlesBufferAccessor()
		{
			Manager->ReadUnlock();
		}

	private:
		FPBDRigidActiveParticlesBuffer* Manager;
	};

}
