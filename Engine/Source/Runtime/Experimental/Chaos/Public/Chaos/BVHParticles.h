// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/BoundingVolumeHierarchy.h"
#include "Chaos/Particles.h"
#include "ChaosArchive.h"
#include "Chaos/ParticleHandleFwd.h"

extern int32 CHAOS_API CollisionParticlesBVHDepth;

namespace Chaos
{
	template<class T, int d>
	class TBVHParticles final /*Note: removing this final has implications for serialization. See TImplicitObject*/ : public TParticles<T, d>
	{
	public:
		using TArrayCollection::Size;
		using TParticles<T, d>::X;
		using TParticles<T, d>::AddParticles;

		TBVHParticles()
		    : TParticles<T, d>()
			, MBVH(*this, CollisionParticlesBVHDepth)
		{}
		TBVHParticles(TBVHParticles<T, d>&& Other)
		    : TParticles<T, d>(MoveTemp(Other))
			, MBVH(MoveTemp(Other.MBVH))
		{}
		TBVHParticles(TParticles<T, d>&& Other)
		    : TParticles<T, d>(MoveTemp(Other))
		    , MBVH(*this, CollisionParticlesBVHDepth)
		{}

		TBVHParticles& operator=(const TBVHParticles<T, d>& Other)
		{
			*this = TBVHParticles(Other);
			return *this;
		}

		TBVHParticles& operator=(TBVHParticles<T, d>&& Other)
		{
			MBVH = MoveTemp(Other.MBVH);
			TParticles<T, d>::operator=(static_cast<TParticles<T, d>&&>(Other));
			return *this;
		}

		TBVHParticles* NewCopy()
		{
			return new TBVHParticles(*this);
		}

		void UpdateAccelerationStructures()
		{
			MBVH.UpdateHierarchy();
		}

		const TArray<int32> FindAllIntersections(const TBox<T, d>& Object) const
		{
			return MBVH.FindAllIntersections(Object);
		}

		static TBVHParticles<T,d>* SerializationFactory(FChaosArchive& Ar, TBVHParticles<T,d>* BVHParticles)
		{
			return Ar.IsLoading() ? new TBVHParticles<T, d>() : nullptr;
		}

		void Serialize(FChaosArchive& Ar)
		{
			TParticles<T, d>::Serialize(Ar);
			Ar << MBVH;
		}

		virtual void Serialize(FArchive& Ar)
		{
			check(false); //Aggregate simplicial require FChaosArchive - check false by default
		}

	private:
		TBVHParticles(const TBVHParticles<T, d>& Other)
			: TParticles<T, d>()
			, MBVH(*this, CollisionParticlesBVHDepth)
		{
			AddParticles(Other.Size());
			for (int32 i = Other.Size() - 1; 0 <= i; i--)
			{
				X(i) = Other.X(i);
			}
			MBVH = TBoundingVolumeHierarchy<TParticles<T, d>, TArray<int32>, T, d>(*this, CollisionParticlesBVHDepth);
		}

		TBoundingVolumeHierarchy<TParticles<T, d>, TArray<int32>, T, d> MBVH;
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
