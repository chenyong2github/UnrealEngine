// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleHandle.h"

#define CHAOS_PARTICLE_ACTORTRANSFORM 0

namespace Chaos
{
	/**
	 * Particle Space == Actor Space (Transforms)
	 * Velocities in CoM Space.
	 */
	class FParticleUtilities_ActorSpace
	{
	public:
		template<typename T_PARTICLEHANDLE>
		static inline FRigidTransform3 GetActorWorldTransform(T_PARTICLEHANDLE Particle)
		{
			return FRigidTransform3(Particle->P(), Particle->Q());
		}

		template<typename T_PARTICLEHANDLE>
		static inline void SetActorWorldTransform(T_PARTICLEHANDLE Particle, const FRigidTransform3& ActorWorldT)
		{
			Particle->P() = ActorWorldT.GetTranslation();
			Particle->Q() = ActorWorldT.GetRotation();
		}

		template<typename T_PARTICLEHANDLE>
		static inline const FRigidTransform3& ActorLocalToParticleLocal(T_PARTICLEHANDLE Particle, const FRigidTransform3& ActorLocalT)
		{
			return ActorLocalT;
		}

		template<typename T_PARTICLEHANDLE>
		static inline const FRigidTransform3& ActorWorldToParticleWorld(T_PARTICLEHANDLE Particle, const FRigidTransform3& ActorWorldT)
		{
			return ActorWorldT;
		}

		/**
		 * Convert an particle position into a com-local-space position
		 */
		template<typename T_PARTICLEHANDLE>
		static inline FVec3 ParticleLocalToCoMLocal(T_PARTICLEHANDLE Particle, const FVec3& P)
		{
			return Particle->RotationOfMass().UnrotateVector(P - Particle->CenterOfMass());
		}

		/**
		 * Convert a particle rotation into a com-local-space rotation
		 */
		template<typename T_PARTICLEHANDLE>
		static inline FRotation3 ParticleLocalToCoMLocal(T_PARTICLEHANDLE Particle, const FRotation3& Q)
		{
			return Particle->RotationOfMass().Inverse()* Q;
		}

		/**
		 * Convert an particle-local-space transform into a com-local-space transform
		 */
		template<typename T_PARTICLEHANDLE>
		static inline FRigidTransform3 ParticleLocalToCoMLocal(T_PARTICLEHANDLE Particle, const FRigidTransform3& T)
		{
			return FRigidTransform3(ParticleLocalToCoMLocal(Particle, T.GetTranslation()), ParticleLocalToCoMLocal(Particle, T.GetRotation()));
		}

		/**
		 * Get the velocity at point 'RelPos', where 'RelPos' is a world-space position relative to the Particle's center of mass.
		 */
		template<typename T_PARTICLEHANDLE>
		static inline FVec3 GetVelocityAtCoMRelativePosition(T_PARTICLEHANDLE Particle, const FVec3& RelPos)
		{
			return Particle->V() + FVec3::CrossProduct(Particle->W(), RelPos);
		}

		template<typename T_PARTICLEHANDLE>
		static inline FVec3 GetCoMWorldPosition(T_PARTICLEHANDLE Particle)
		{
			return Particle->P() + Particle->Q().RotateVector(Particle->CenterOfMass());
		}

		static inline FVec3 GetCoMWorldPosition(TPBDRigidParticles<FReal, 3>& Particles, const int32 Index)
		{
			return Particles.P(Index) + Particles.Q(Index).RotateVector(Particles.CenterOfMass(Index));
		}

		template<typename T_PARTICLEHANDLE>
		static inline FRotation3 GetCoMWorldRotation(T_PARTICLEHANDLE Particle)
		{
			return Particle->Q() * Particle->RotationOfMass();
		}
	
		static inline FRotation3 GetCoMWorldRotation(TPBDRigidParticles<FReal, 3>& Particles, const int32 Index)
		{
			return Particles.Q(Index) * Particles.RotationOfMass(Index);
		}

		template<typename T_PARTICLEHANDLE>
		static inline FRigidTransform3 GetCoMWorldTransform(T_PARTICLEHANDLE Particle)
		{
			return FRigidTransform3(GetCoMWorldPosition(Particle), GetCoMWorldRotation(Particle));
		}

		/**
		 * Update the particle's position and rotation by specifying a new center of mass transform.
		 */
		template<typename T_PARTICLEHANDLE>
		static inline void SetCoMWorldTransform(T_PARTICLEHANDLE Particle, const FVec3& PCoM, const FRotation3& QCoM)
		{
			const FRotation3 Q = QCoM * Particle->RotationOfMass().Inverse();
			const FVec3 P = PCoM - Q.RotateVector(Particle->CenterOfMass());
			
			Particle->P() = P;
			Particle->Q() = Q;
		}

		static inline void SetCoMWorldTransform(TPBDRigidParticles<FReal, 3>& Particles, const int32 Index, const FVec3& PCoM, const FRotation3& QCoM)
		{
			const FRotation3 Q = QCoM * Particles.RotationOfMass(Index).Inverse();
			const FVec3 P = PCoM - Q.RotateVector(Particles.CenterOfMass(Index));

			Particles.P(Index) = P;
			Particles.Q(Index) = Q;
		}
	};

	/**
	 * Particle Space == CoM Space.
	 * Velocities in CoM Space.
	 */
	class FParticleUtilities_CoMSpace
	{
	public:
		template<typename T_PARTICLEHANDLE>
		static inline FRigidTransform3 GetActorWorldTransform(T_PARTICLEHANDLE Particle)
		{
			FRotation3 ActorQ = Particle->Q() * Particle->RotationOfMass().Inverse();
			FVec3 ActorP = Particle->P() - ActorQ.RotateVector(Particle->CenterOfMass());
			return FRigidTransform3(ActorP, ActorQ);
		}

		template<typename T_PARTICLEHANDLE>
		static inline void SetActorWorldTransform(T_PARTICLEHANDLE Particle, const FRigidTransform3& ActorWorldT)
		{
			FRotation3 CoMQ = ActorWorldT.GetRotation() * Particle->RotationOfMass();
			FVec3 CoMP = ActorWorldT.GetTranslation() + ActorWorldT.GetRotation().RotateVector(Particle->CenterOfMass());
			Particle->P() = CoMP;
			Particle->Q() = CoMQ;
		}

		template<typename T_PARTICLEHANDLE>
		static inline FRigidTransform3 ActorLocalToParticleLocal(T_PARTICLEHANDLE Particle, const FRigidTransform3& ActorLocalT)
		{
			return FRigidTransform3(Particle->RotationOfMass().UnrotateVector(ActorLocalT.GetTranslation() - Particle->CenterOfMass()), Particle->RotationOfMass().Inverse() * ActorLocalT.GetRotation());
		}

		template<typename T_PARTICLEHANDLE>
		static inline FRigidTransform3 ActorWorldToParticleWorld(T_PARTICLEHANDLE Particle, const FRigidTransform3& ActorWorldT)
		{
			FRotation3 CoMQ = ActorWorldT.GetRotation() * Particle->RotationOfMass();
			FVec3 CoMP = ActorWorldT.GetTranslation() + ActorWorldT.GetRotation().RotateVector(Particle->CenterOfMass());
			return FRigidTransform3(CoMP, CoMQ);
		}

		template<typename T_PARTICLEHANDLE>
		static inline const FVec3& ParticleLocalToCoMLocal(T_PARTICLEHANDLE Particle, const FVec3& P)
		{
			return P;
		}

		template<typename T_PARTICLEHANDLE>
		static inline const FRotation3& ParticleLocalToCoMLocal(T_PARTICLEHANDLE Particle, const FRotation3& Q)
		{
			return Q;
		}

		template<typename T_PARTICLEHANDLE>
		static inline const FRigidTransform3& ParticleLocalToCoMLocal(T_PARTICLEHANDLE Particle, const FRigidTransform3& T)
		{
			return T;
		}

		template<typename T_PARTICLEHANDLE>
		static inline FVec3 GetVelocityAtCoMRelativePosition(T_PARTICLEHANDLE Particle, const FVec3& RelPos)
		{
			return Particle->V() + FVec3::CrossProduct(Particle->W(), RelPos);
		}

		template<typename T_PARTICLEHANDLE>
		static inline const FVec3& GetCoMWorldPosition(T_PARTICLEHANDLE Particle)
		{
			return Particle->P();
		}

		static inline const FVec3& GetCoMWorldPosition(TPBDRigidParticles<FReal, 3>& Particles, const int32 Index)
		{
			return Particles.P(Index);
		}

		template<typename T_PARTICLEHANDLE>
		static inline const FRotation3& GetCoMWorldRotation(T_PARTICLEHANDLE Particle)
		{
			return Particle->Q();
		}

		template<typename T_PARTICLEHANDLE>
		static inline FRigidTransform3 GetCoMWorldTransform(T_PARTICLEHANDLE Particle)
		{
			return FRigidTransform3(GetCoMWorldRotation(Particle), GetCoMWorldPosition(Particle));
		}

		static inline const FRotation3& GetCoMWorldRotation(TPBDRigidParticles<FReal, 3>& Particles, const int32 Index)
		{
			return Particles.Q(Index);
		}

		template<typename T_PARTICLEHANDLE>
		static inline void SetCoMWorldTransform(T_PARTICLEHANDLE Particle, const FVec3& PCoM, const FRotation3& QCoM)
		{
			Particle->P() = PCoM;
			Particle->Q() = QCoM;
		}

		static inline void SetCoMWorldTransform(TPBDRigidParticles<FReal, 3>& Particles, const int32 Index, const FVec3& PCoM, const FRotation3& QCoM)
		{
			Particles.P(Index) = PCoM;
			Particles.Q(Index) = QCoM;
		}
	};

#if CHAOS_PARTICLE_ACTORTRANSFORM
	using FParticleUtilities = FParticleUtilities_ActorSpace;
#else
	using FParticleUtilities = FParticleUtilities_CoMSpace;
#endif

}