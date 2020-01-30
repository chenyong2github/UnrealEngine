// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/BVHParticles.h"
#include "Chaos/BoundingVolumeHierarchy.h"

using namespace Chaos;

template<class T, int d>
TBVHParticles<T,d>::TBVHParticles()
	: TParticles<T, d>()
	, MBVH(new TBoundingVolumeHierarchy<TParticles<T, d>, TArray<int32>, T, d>(*this, CollisionParticlesBVHDepth))
{
}

template<class T, int d>
TBVHParticles<T,d>::TBVHParticles(TBVHParticles<T, d>&& Other)
	: TParticles<T, d>(MoveTemp(Other))
	, MBVH(new TBoundingVolumeHierarchy<TParticles<T, d>, TArray<int32>, T, d>(MoveTemp(*Other.MBVH)))
{
}

template<class T, int d>
TBVHParticles<T,d>::TBVHParticles(TParticles<T, d>&& Other)
	: TParticles<T, d>(MoveTemp(Other))
	, MBVH(new TBoundingVolumeHierarchy<TParticles<T, d>, TArray<int32>, T, d>(*this, CollisionParticlesBVHDepth))
{
}

template<class T, int d>
TBVHParticles<T,d>::~TBVHParticles()
{
    delete MBVH;
}

template<class T, int d>
TBVHParticles<T,d>& TBVHParticles<T,d>::operator=(const TBVHParticles<T, d>& Other)
{
	*this = TBVHParticles(Other);
	return *this;
}

template<class T, int d>
TBVHParticles<T,d>& TBVHParticles<T,d>::operator=(TBVHParticles<T, d>&& Other)
{
	*MBVH = MoveTemp(*Other.MBVH);
	TParticles<T, d>::operator=(static_cast<TParticles<T, d>&&>(Other));
	return *this;
}

template<class T, int d>
TBVHParticles<T,d>::TBVHParticles(const TBVHParticles<T, d>& Other)
	: TParticles<T, d>()
{
	AddParticles(Other.Size());
	for (int32 i = Other.Size() - 1; 0 <= i; i--)
	{
		X(i) = Other.X(i);
	}
	MBVH = new TBoundingVolumeHierarchy<TParticles<T, d>, TArray<int32>, T, d>(*this, CollisionParticlesBVHDepth);
}

template<class T, int d>
void TBVHParticles<T,d>::UpdateAccelerationStructures()
{
	MBVH->UpdateHierarchy();
}

template<class T, int d>
const TArray<int32> TBVHParticles<T,d>::FindAllIntersections(const TAABB<T, d>& Object) const
{
	return MBVH->FindAllIntersections(Object);
}

template<class T, int d>
void TBVHParticles<T,d>::Serialize(FChaosArchive& Ar)
{
	TParticles<T, d>::Serialize(Ar);
	Ar << *MBVH;
}

template class Chaos::TBVHParticles<float,3>;
