// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Particles.h"
#include "ChaosArchive.h"
#include "Chaos/ParticleHandleFwd.h"


namespace Chaos
{
	extern int32 CHAOS_API CollisionParticlesBVHDepth;

    template<class OBJECT_ARRAY, class LEAF_TYPE, class T, int d>
    class TBoundingVolumeHierarchy;


    template<class OBJECT_ARRAY, class LEAF_TYPE, class T, int d>
    class TBoundingVolumeHierarchy;

    template<class T, int d>
    class TAABB;

	template<class T, int d>
	class TBVHParticles final /*Note: removing this final has implications for serialization. See TImplicitObject*/ : public TParticles<T, d>
	{
	public:
		using TArrayCollection::Size;
		using TParticles<T, d>::X;
		using TParticles<T, d>::AddParticles;

		CHAOS_API TBVHParticles();
		TBVHParticles(TBVHParticles<T, d>&& Other);
		TBVHParticles(TParticles<T, d>&& Other);
        CHAOS_API ~TBVHParticles();

	    CHAOS_API TBVHParticles& operator=(const TBVHParticles<T, d>& Other);
	    CHAOS_API TBVHParticles& operator=(TBVHParticles<T, d>&& Other);

		CHAOS_API TBVHParticles* NewCopy()
		{
			return new TBVHParticles(*this);
		}

		CHAOS_API void UpdateAccelerationStructures();
		const TArray<int32> FindAllIntersections(const TAABB<T, d>& Object) const;

		static TBVHParticles<T,d>* SerializationFactory(FChaosArchive& Ar, TBVHParticles<T,d>* BVHParticles)
		{
			return Ar.IsLoading() ? new TBVHParticles<T, d>() : nullptr;
		}

		CHAOS_API void Serialize(FChaosArchive& Ar);

	private:
		CHAOS_API TBVHParticles(const TBVHParticles<T, d>& Other);

		TBoundingVolumeHierarchy<TParticles<T, d>, TArray<int32>, T, d>* MBVH;
	};

	template<typename T, int d>
	FORCEINLINE FChaosArchive& operator<<(FChaosArchive& Ar, TBVHParticles<T, d>& Value)
	{
		Value.Serialize(Ar);
		return Ar;
	}

	template<typename T, int d>
	FORCEINLINE FArchive& operator<<(FArchive& Ar, TBVHParticles<T, d>& Value)
	{
		Value.Serialize(Ar);
		return Ar;
	}

	typedef TBVHParticles<float, 3> FBVHParticlesFloat3;
}
