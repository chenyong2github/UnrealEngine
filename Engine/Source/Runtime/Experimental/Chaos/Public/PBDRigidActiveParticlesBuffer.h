// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"

class FSingleParticlePhysicsProxy;

namespace Chaos
{
	/**
	 * Solver specific data buffered for use on Game thread
	 */
	struct CHAOS_API FPBDRigidDirtyParticlesBufferOut
	{
		TArray<FSingleParticlePhysicsProxy*> DirtyGameThreadParticles;
		// Some particle types (clustered) only exist on the game thread, but we
		// still need to pull data over via their proxies.
		TSet<IPhysicsProxyBase*> PhysicsParticleProxies;
	};


	class CHAOS_API FPBDRigidDirtyParticlesBuffer
	{
		friend class FPBDRigidDirtyParticlesBufferAccessor;

	public:
		FPBDRigidDirtyParticlesBuffer(const Chaos::EMultiBufferMode& InBufferMode, bool bInSingleThreaded);

		template <typename Traits>
		void CaptureSolverData(TPBDRigidsSolver<Traits>* Solver);

		void ReadLock();
		void ReadUnlock();
		void WriteLock();
		void WriteUnlock();
	
	private:
		const FPBDRigidDirtyParticlesBufferOut* GetSolverOutData() const
		{
			return SolverDataOut->GetConsumerBuffer();
		}

		/**
		 * Fill data from solver destined for the game thread - used to limit the number of objects updated on the game thread
		 */
		template <typename Traits>
		void BufferPhysicsResults(TPBDRigidsSolver<Traits>* Solver);

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
		TUniquePtr<IBufferResource<FPBDRigidDirtyParticlesBufferOut>> SolverDataOut;
	};

	class FPBDRigidDirtyParticlesBufferAccessor
	{
	public:
		FPBDRigidDirtyParticlesBufferAccessor(FPBDRigidDirtyParticlesBuffer* InManager) : Manager(InManager)
		{
			check(InManager);
			Manager->ReadLock();
		}

		const FPBDRigidDirtyParticlesBufferOut* GetSolverOutData() const
		{
			return Manager->GetSolverOutData();
		}

		~FPBDRigidDirtyParticlesBufferAccessor()
		{
			Manager->ReadUnlock();
		}

	private:
		FPBDRigidDirtyParticlesBuffer* Manager;
	};


#define EVOLUTION_TRAIT(Trait) extern template CHAOS_TEMPLATE_API void Chaos::FPBDRigidDirtyParticlesBuffer::BufferPhysicsResults<Trait>(TPBDRigidsSolver<Trait>* Solver);
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT

#define EVOLUTION_TRAIT(Trait) extern template CHAOS_TEMPLATE_API void Chaos::FPBDRigidDirtyParticlesBuffer::CaptureSolverData<Trait>(TPBDRigidsSolver<Trait>* Solver);
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT

}
