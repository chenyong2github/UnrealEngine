// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	inline uint32 OrderIndependentHashCombine(const uint32 A, const uint32 B)
	{
		if (A < B)
		{
			return ::HashCombine(A, B);
		}
		else
		{
			return ::HashCombine(B, A);
		}
	}

	/**
	 * @brief Order particles in a consistent way for use by Broadphase and Resim
	*/
	inline bool ShouldSwapParticleOrder(const FGeometryParticleHandle* Particle0, const FGeometryParticleHandle* Particle1)
	{
		const bool bIsParticle1Preferred = (Particle1->ParticleID() < Particle0->ParticleID());
		const bool bSwapOrder = !FConstGenericParticleHandle(Particle0)->IsDynamic() || !bIsParticle1Preferred;
		return bSwapOrder;
	}

	/**
	 * @brief A key which uniquely identifes a particle pair for use by the collision detection system
	 * This key will be the same if particles order is reversed.
	 * @note This uses ParticleID and truncates it from 32 to 31 bits
	*/
	class FCollisionParticlePairKey
	{
	public:
		using KeyType = uint64;

		FCollisionParticlePairKey()
		{
			Key.Key64 = 0;
		}

		FCollisionParticlePairKey(const FGeometryParticleHandle* Particle0, const FGeometryParticleHandle* Particle1)
		{
			GenerateKey(Particle0, Particle1);
		}

		uint64 GetKey() const
		{
			return Key.Key64;
		}

	private:
		void GenerateKey(const FGeometryParticleHandle* Particle0, const FGeometryParticleHandle* Particle1)
		{
			const bool bIsLocalID0 = Particle0->ParticleID().LocalID != INDEX_NONE;
			const bool bIsLocalID1 = Particle1->ParticleID().LocalID != INDEX_NONE;
			const uint32 ID0 = uint32((bIsLocalID0) ? Particle0->ParticleID().LocalID : Particle0->ParticleID().GlobalID);
			const uint32 ID1 = uint32((bIsLocalID1) ? Particle1->ParticleID().LocalID : Particle1->ParticleID().GlobalID);

			if (ID0 < ID1)
			{
				Key.Key32s[0].Key31 = ID0;
				Key.Key32s[0].IsLocal = bIsLocalID0;
				Key.Key32s[1].Key31 = ID1;
				Key.Key32s[1].IsLocal = bIsLocalID1;
			}
			else
			{
				Key.Key32s[0].Key31 = ID1;
				Key.Key32s[0].IsLocal = bIsLocalID1;
				Key.Key32s[1].Key31 = ID0;
				Key.Key32s[1].IsLocal = bIsLocalID0;
			}
		}

		struct FParticleIDKey
		{
			uint32 Key31 : 31;
			uint32 IsLocal : 1;
		};
		union FIDKey
		{
			uint64 Key64;
			FParticleIDKey Key32s[2];
		};

		// This class is sensitive to changes in FParticleID - try to catch that here...
		static_assert(sizeof(FParticleID) == 8, "FParticleID size does not match FCollisionParticlePairKey (expected 64 bits)");
		static_assert(sizeof(FParticleID::GlobalID) == 4, "FParticleID::GlobalID size does not match FCollisionParticlePairKey (expected 32 bits)");
		static_assert(sizeof(FParticleID::LocalID) == 4, "FParticleID::LocalID size does not match FCollisionParticlePairKey (expected 32 bits)");
		static_assert(sizeof(FParticleIDKey) == 4, "FCollisionParticlePairKey::FParticleIDKey size is not 32 bits");
		static_assert(sizeof(FIDKey) == 8, "FCollisionParticlePairKey::FIDKey size is not 64 bits");

		FIDKey Key;
	};

	/**
	 * @brief A key which uniquely identifes a collision constraint within a particle pair
	 * 
	 * This key only needs to be uinque within the context of a particle pair. There is no
	 * guarantee of global uniqueness. This key is only used by the FMultiShapePairCollisionDetector
	 * class which is used for colliding shape pairs where each shape is actually a hierarchy
	 * of shapes. 
	 * 
	*/
	class FCollisionParticlePairConstraintKey
	{
	public:
		FCollisionParticlePairConstraintKey()
			: Key(0)
		{
		}

		FCollisionParticlePairConstraintKey(const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1)
			: Key(0)
		{
			check((Implicit0 != nullptr) || (Simplicial0 != nullptr));
			check((Implicit1 != nullptr) || (Simplicial1 != nullptr));
			GenerateHash(Implicit0, Simplicial0, Implicit1, Simplicial1);
		}

		uint32 GetKey() const
		{
			return Key;
		}

		friend bool operator==(const FCollisionParticlePairConstraintKey& L, const FCollisionParticlePairConstraintKey& R)
		{
			return L.Key == R.Key;
		}

		friend bool operator!=(const FCollisionParticlePairConstraintKey& L, const FCollisionParticlePairConstraintKey& R)
		{
			return !(L == R);
		}

		friend bool operator<(const FCollisionParticlePairConstraintKey& L, const FCollisionParticlePairConstraintKey& R)
		{
			return L.Key < R.Key;
		}

	private:
		void GenerateHash(const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1)
		{
			const uint32 Hash0 = (Implicit0 != nullptr) ? ::GetTypeHash(Implicit0) : ::GetTypeHash(Simplicial0);
			const uint32 Hash1 = (Implicit1 != nullptr) ? ::GetTypeHash(Implicit1) : ::GetTypeHash(Simplicial1);
			Key = OrderIndependentHashCombine(Hash0, Hash1);
		}

		uint32 Key;
	};
}