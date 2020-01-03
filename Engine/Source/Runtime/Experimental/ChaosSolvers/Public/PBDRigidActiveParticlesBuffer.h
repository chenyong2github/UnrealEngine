// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	class FPBDRigidsSolver;

	/**
	 * Solver specific data buffered for use on Game thread
	 */
	struct CHAOSSOLVERS_API FPBDRigidActiveParticlesBufferOut
	{
		TArray<TGeometryParticle<float, 3>*> ActiveGameThreadParticles;
	};


	class CHAOSSOLVERS_API FPBDRigidActiveParticlesBuffer
	{
		friend class FPBDRigidActiveParticlesBufferAccessor;

	public:
		FPBDRigidActiveParticlesBuffer(const Chaos::EMultiBufferMode& InBufferMode);

		void CaptureSolverData(FPBDRigidsSolver* Solver);
	
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

		// Physics thread to game thread
		TUniquePtr<IBufferResource<FPBDRigidActiveParticlesBufferOut>> SolverDataOut;

	};

	class FPBDRigidActiveParticlesBufferAccessor
	{
	public:
		FPBDRigidActiveParticlesBufferAccessor(FPBDRigidActiveParticlesBuffer* InManager) : Manager(InManager)
		{
			check(InManager);
			Manager->ResourceOutLock.ReadLock();
		}

		const FPBDRigidActiveParticlesBufferOut* GetSolverOutData() const
		{
			return Manager->GetSolverOutData();
		}

		~FPBDRigidActiveParticlesBufferAccessor()
		{
			Manager->ResourceOutLock.ReadUnlock();
		}

	private:
		FPBDRigidActiveParticlesBuffer* Manager;
	};

}
