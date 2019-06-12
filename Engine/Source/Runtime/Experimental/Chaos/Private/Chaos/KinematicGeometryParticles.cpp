// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	template<class T, int d>
	const typename TKinematicGeometryParticles<T, d>::THandleType* TKinematicGeometryParticles<T, d>::Handle(int32 Index) const
	{
		return static_cast<const TKinematicGeometryParticles<T, d>::THandleType*>(TGeometryParticles<T, d>::Handle(Index));
	}

	template<class T, int d>
	typename TKinematicGeometryParticles<T, d>::THandleType* Chaos::TKinematicGeometryParticles<T, d>::Handle(int32 Index)
	{
		return static_cast<TKinematicGeometryParticles<T, d>::THandleType*>(TGeometryParticles<T, d>::Handle(Index));
	}
}

template class Chaos::TKinematicGeometryParticles<float, 3>;
// Re-enable when double precision is able to compile
//template Chaos::TKinematicGeometryParticles<double, 3>;