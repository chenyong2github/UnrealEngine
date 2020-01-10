// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// Use to define out code blocks that need to be adapted to use Particle Handles in a searchable way (better than #if 0)
#define CHAOS_PARTICLEHANDLE_TODO 0

namespace Chaos
{
	// TGeometryParticle 

	template <typename T, int d>
	class TGeometryParticle;
	using TGeometryParticleFloat3 = TGeometryParticle<float, 3>;

	template <typename T, int d, bool bProcessing>
	class TGeometryParticleHandleImp;

	template <typename T, int d>
	using TGeometryParticleHandle = TGeometryParticleHandleImp<T, d, true>;

	template <typename T, int d>
	using TTransientGeometryParticleHandle = TGeometryParticleHandleImp<T, d, false>;

	// TKinematicGeometryParticle

	template <typename T, int d>
	class TKinematicGeometryParticle;

	template <typename T, int d, bool bProcessing>
	class TKinematicGeometryParticleHandleImp;

	template <typename T, int d>
	using TKinematicGeometryParticleHandle = TKinematicGeometryParticleHandleImp<T, d, true>;

	template <typename T, int d>
	using TTransientKinematicGeometryParticleHandle = TKinematicGeometryParticleHandleImp<T, d, false>;

	// TPBDRigidParticle

	template <typename T, int d>
	class TPBDRigidParticle;

	template <typename T, int d, bool bProcessing>
	class TPBDRigidParticleHandleImp;

	template <typename T, int d>
	using TPBDRigidParticleHandle = TPBDRigidParticleHandleImp<T, d, true>;
	using TPBDRigidParticleHandleFloat3 = TPBDRigidParticleHandle<float, 3>;

	template <typename T, int d>
	using TTransientPBDRigidParticleHandle = TPBDRigidParticleHandleImp<T, d, false>;

	// TPBDRigidClusteredParticleHandle

	template <typename T, int d, bool bProcessing>
	class TPBDRigidClusteredParticleHandleImp;

	template <typename T, int d>
	using TPBDRigidClusteredParticleHandle = TPBDRigidClusteredParticleHandleImp<T, d, true>;

	template <typename T, int d>
	using TTransientPBDRigidClusteredParticleHandle = TPBDRigidClusteredParticleHandleImp<T, d, false>;

	// TPBDGeometryCollectionParticleHandle

	template <typename T, int d>
	class TPBDGeometryCollectionParticle;

	template <typename T, int d, bool bPersistent>
	class TPBDGeometryCollectionParticleHandleImp;

	template <typename T, int d>
	using TPBDGeometryCollectionParticleHandle = TPBDGeometryCollectionParticleHandleImp<T, d, true>;
	using TPBDGeometryCollectionParticleHandleFloat3 = TPBDGeometryCollectionParticleHandle<float, 3>;

	template <typename T, int d>
	using TTransientPBDGeometryCollectionParticleHandle = TPBDGeometryCollectionParticleHandleImp<T, d, false>;
	
	// TGenericParticleHandle

	template <typename T, int d>
	class TGenericParticleHandle;

	// TParticleIterator

	template <typename TSOA>
	class TParticleIterator;

	template <typename TSOA>
	class TConstParticleIterator;
}
