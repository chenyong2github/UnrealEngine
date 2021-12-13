// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"

#include "Chaos/Collision/CollisionVisitor.h"
#include "Chaos/ParticleHandleFwd.h"

namespace Chaos
{
	class FParticlePairMidPhase;


	/**
	 * @brief Knows about all the collisions detectors associated with a particular particle.
	 * Used when particles are destroyed to remove perisstent collisions from the system, or
	 * when Islands are woken to restore the collisions.
	 * 
	*/
	class CHAOS_API FParticleCollisions
	{
	public:
		// In a mostly stationary scene the choice of container doesn't matter much. In a highly dynamic
		// scene it matters a lot. This was tested with TMap, TSortedMap, and TArray. TArray was better in all cases.
		// We store the list of overlapping particle pairs for every particle. This list can get quite
		// large for world objects. We need to make sure that Add / Remove are not O(N).
		// We also search this list to find an existing MidPhase for a specific particle pair. However
		// we always search the particle with the fewest overlaps, which will be a dynamic particle
		// that should not have too many contacts. 
		using FContainerType = TArray<TPair<uint64, FParticlePairMidPhase*>>;

		FParticleCollisions();
		~FParticleCollisions();

		int32 Num() const 
		{ 
			return MidPhases.Num();
		}

		/**
		 * @brief Clear the list of midphases. Only for use in shutdown.
		*/
		void Reset()
		{
			MidPhases.Reset();
		}

		/**
		 * @brief Add a mid phase to the list
		 * We are passing the particle and midphase here rather than just the key because we store a cookie
		 * on the midphase that we want to retrieve, and it has one cookie per particle. 
		 * This could probably be cleaned up a bit...
		*/
		void AddMidPhase(FGeometryParticleHandle* InParticle, FParticlePairMidPhase* InMidPhase);

		/**
		 * @brief Remove a mid phase
		*/
		void RemoveMidPhase(FGeometryParticleHandle* InParticle, FParticlePairMidPhase* InMidPhase);

		/**
		 * @brief Find the mid phase with the matching key
		 * @param InKey The internal key from a FCollisionParticlePairKey
		 * @todo(chaos): we should use FCollisionParticlePairKey here
		*/
		inline FParticlePairMidPhase* FindMidPhase(const uint64 InKey)
		{
			for (int32 Index = 0; Index < MidPhases.Num(); ++Index)
			{
				if (MidPhases[Index].Key == InKey)
				{
					return MidPhases[Index].Value;
				}
			}
			return nullptr;
		}

		/**
		 * @brief Visit all of the midphases on the particle and call the specified function
		 * @note Do not call RemoveMidPhase from the visitor
		 * 
		 * Lambda must have signature void(FParticlePairMidPhase&)
		*/
		template<typename TLambda>
		void VisitMidPhases(const TLambda& Lambda)
		{
			for (int32 Index = 0; Index < MidPhases.Num(); ++Index)
			{
				Lambda(*MidPhases[Index].Value);
			}
		}

		/**
		 * @brief Visit all of the midphases on the particle and call the specified function
		 * @note Do not call RemoveMidPhase from the visitor
		 *
		 * Lambda must have signature void(const FParticlePairMidPhase&)
		*/
		template<typename TLambda>
		void VisitConstMidPhases(const TLambda& Lambda) const
		{
			for (int32 Index = 0; Index < MidPhases.Num(); ++Index)
			{
				Lambda(*MidPhases[Index].Value);
			}
		}

		void VisitCollisions(const FPBDCollisionVisitor& Visitor) const;


	private:
		FContainerType MidPhases;
	};

}