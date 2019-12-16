// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

// Use to define out code blocks that need to be adapted to use Particle Handles in a searchable way (better than #if 0)
#define CHAOS_PARTICLEHANDLE_TODO 0

namespace Chaos
{
	template <typename T, int d, bool bProcessing>
	class TGeometryParticleHandleImp;

	template <typename T, int d, bool bProcessing>
	class TKinematicGeometryParticleHandleImp;

	template <typename T, int d, bool bProcessing>
	class TPBDRigidParticleHandleImp;

	template <typename T, int d, bool bProcessing>
	class TPBDRigidClusteredParticleHandleImp;

	template <typename T, int d>
	using TGeometryParticleHandle = TGeometryParticleHandleImp<T, d, true>;

	template <typename T, int d>
	using TTransientGeometryParticleHandle = TGeometryParticleHandleImp<T, d, false>;

	template <typename T, int d>
	using TKinematicGeometryParticleHandle = TKinematicGeometryParticleHandleImp<T, d, true>;

	template <typename T, int d>
	using TTransientKinematicGeometryParticleHandle = TKinematicGeometryParticleHandleImp<T, d, false>;

	template <typename T, int d>
	using TPBDRigidParticleHandle = TPBDRigidParticleHandleImp<T, d, true>;

	template <typename T, int d>
	using TTransientPBDRigidParticleHandle = TPBDRigidParticleHandleImp<T, d, false>;

	template <typename T, int d>
	using TPBDRigidClusteredParticleHandle = TPBDRigidClusteredParticleHandleImp<T, d, true>;

	template <typename T, int d>
	using TTransientPBDRigidClusteredParticleHandle = TPBDRigidClusteredParticleHandleImp<T, d, false>;

	template <typename T, int d>
	class TGenericParticleHandle;

	template <typename TSOA>
	class TParticleIterator;

	template <typename TSOA>
	class TConstParticleIterator;

	template <typename T, int d>
	class TGeometryParticle;

	template <typename T, int d>
	class TKinematicGeometryParticle;

	template <typename T, int d>
	class TPBDRigidParticle;
}