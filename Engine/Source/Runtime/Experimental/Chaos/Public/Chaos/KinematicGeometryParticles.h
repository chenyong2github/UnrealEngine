// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/GeometryParticles.h"

namespace Chaos
{
	/**
	 * Controls how a kinematic body is integrated each Evolution Advance
	 */
	enum class EKinematicTargetMode
	{
		None,			/** Particle does not move and no data is changed */
		Zero,			/** Particle does not move, velocity and angular velocity are zeroed, then mode is set to "None". */
		Position,		/** Particle is moved to Kinematic Target transform, velocity and angular velocity updated to reflect the change, then mode is reset to "Zero". */
		Velocity,		/** Particle is moved based on velocity and angular velocity, mode remains as "Velocity". */
	};

	/**
	 * Data used to integrate kinematic bodies
	 */
	template<class T, int d>
	class CHAOS_API TKinematicTarget
	{
	public:
		TKinematicTarget()
			: Mode(EKinematicTargetMode::None)
		{
		}

		/** Whether this kinematic target has been set (either velocity or position mode) */
		bool IsSet() const { return (Mode == EKinematicTargetMode::Position) || (Mode == EKinematicTargetMode::Velocity); }

		/** Get the kinematic target mode */
		EKinematicTargetMode GetMode() const { return Mode; }

		/** Get the target transform (asserts if not in Position mode) */
		const TRigidTransform<T, d>& GetTarget() const { check(Mode == EKinematicTargetMode::Position); return Target; }

		/** Use transform target mode and set the transform target */
		void SetTargetMode(const TRigidTransform<T, d>& InTarget) { Target = InTarget;  Mode = EKinematicTargetMode::Position; }

		/** Use velocity target mode */
		void SetVelocityMode() { Mode = EKinematicTargetMode::Velocity; }

		// For internal use only
		void SetMode(EKinematicTargetMode InMode) { Mode = InMode; }

		friend FChaosArchive& operator<<(FChaosArchive& Ar, TKinematicTarget<T, d>& KinematicTarget)
		{
			Ar << KinematicTarget.Target << KinematicTarget.Mode;
			return Ar;
		}

	private:
		TRigidTransform<T, d> Target;
		EKinematicTargetMode Mode;
	};


template<class T, int d, EGeometryParticlesSimType SimType>
class TKinematicGeometryParticlesImp : public TGeometryParticlesImp<T, d, SimType>
{
  public:
	CHAOS_API TKinematicGeometryParticlesImp()
	    : TGeometryParticlesImp<T, d, SimType>()
	{
		this->MParticleType = EParticleType::Kinematic;
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MW);
		TArrayCollection::AddArray(&KinematicTargets);
	}
	TKinematicGeometryParticlesImp(const TKinematicGeometryParticlesImp<T, d, SimType>& Other) = delete;
	CHAOS_API TKinematicGeometryParticlesImp(TKinematicGeometryParticlesImp<T, d, SimType>&& Other)
	    : TGeometryParticlesImp<T, d, SimType>(MoveTemp(Other)), MV(MoveTemp(Other.MV)), MW(MoveTemp(Other.MW))
	{
		this->MParticleType = EParticleType::Kinematic;
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MW);
		TArrayCollection::AddArray(&KinematicTargets);
	}
	CHAOS_API virtual ~TKinematicGeometryParticlesImp();

	const TVector<T, d>& V(const int32 Index) const { return MV[Index]; }
	TVector<T, d>& V(const int32 Index) { return MV[Index]; }

	const TVector<T, d>& W(const int32 Index) const { return MW[Index]; }
	TVector<T, d>& W(const int32 Index) { return MW[Index]; }

	const TKinematicTarget<T, d>& KinematicTarget(const int32 Index) const { return KinematicTargets[Index]; }
	TKinematicTarget<T, d>& KinematicTarget(const int32 Index) { return KinematicTargets[Index]; }

	FString ToString(int32 index) const
	{
		FString BaseString = TGeometryParticlesImp<T, d, SimType>::ToString(index);
		return FString::Printf(TEXT("%s, MV:%s, MW:%s"), *BaseString, *V(index).ToString(), *W(index).ToString());
	}

	typedef TKinematicGeometryParticleHandle<T, d> THandleType;
	const THandleType* Handle(int32 Index) const;

	//cannot be reference because double pointer would allow for badness, but still useful to have non const access to handle
	THandleType* Handle(int32 Index);

	CHAOS_API virtual void Serialize(FChaosArchive& Ar) override
	{
		TGeometryParticlesImp<T, d, SimType>::Serialize(Ar);
		Ar << MV << MW;
		
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::KinematicTargets)
		{
			Ar << KinematicTargets;
		}
	}

  private:
	TArrayCollectionArray<TVector<T, d>> MV;
	TArrayCollectionArray<TVector<T, d>> MW;
	TArrayCollectionArray<TKinematicTarget<T, d>> KinematicTargets;
};

template <typename T, int d, EGeometryParticlesSimType SimType>
FChaosArchive& operator<<(FChaosArchive& Ar, TKinematicGeometryParticlesImp<T, d, SimType>& Particles)
{
	Particles.Serialize(Ar);
	return Ar;
}

#if PLATFORM_MAC || PLATFORM_LINUX
extern template class CHAOS_API Chaos::TKinematicGeometryParticlesImp<float, 3, Chaos::EGeometryParticlesSimType::RigidBodySim>;
extern template class CHAOS_API Chaos::TKinematicGeometryParticlesImp<float, 3, Chaos::EGeometryParticlesSimType::Other>;
#else
extern template class Chaos::TKinematicGeometryParticlesImp<float, 3, Chaos::EGeometryParticlesSimType::RigidBodySim>;
extern template class Chaos::TKinematicGeometryParticlesImp<float, 3, Chaos::EGeometryParticlesSimType::Other>;
#endif

template <typename T, int d>
using TKinematicGeometryParticles = TKinematicGeometryParticlesImp<T, d, EGeometryParticlesSimType::RigidBodySim>;

template <typename T, int d>
using TKinematicGeometryClothParticles = TKinematicGeometryParticlesImp<T, d, EGeometryParticlesSimType::Other>;
}
