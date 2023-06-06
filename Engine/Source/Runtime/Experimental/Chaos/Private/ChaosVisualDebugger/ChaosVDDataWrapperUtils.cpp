// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVisualDebugger/ChaosVDDataWrapperUtils.h"

#if WITH_CHAOS_VISUAL_DEBUGGER
#include "Chaos/ParticleHandle.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"

FChaosVDParticleDataWrapper FChaosVDDataWrapperUtils::BuildParticleDataWrapperFromParticle(const Chaos::FGeometryParticleHandle* ParticleHandlePtr)
{
	check(ParticleHandlePtr);

	FChaosVDParticleDataWrapper WrappedParticleData;

	WrappedParticleData.ParticleIndex = ParticleHandlePtr->UniqueIdx().Idx;
	WrappedParticleData.Type =  static_cast<EChaosVDParticleType>(ParticleHandlePtr->Type);

#if CHAOS_DEBUG_NAME
	WrappedParticleData.DebugNamePtr = ParticleHandlePtr->DebugName();
#endif

	WrappedParticleData.ParticlePositionRotation.CopyFrom(*ParticleHandlePtr);

	if (const Chaos::TKinematicGeometryParticleHandleImp<Chaos::FReal, 3, true>* KinematicParticle = ParticleHandlePtr->CastToKinematicParticle())
	{
		WrappedParticleData.ParticleVelocities.CopyFrom(*KinematicParticle);
	}

	if (const Chaos::TPBDRigidParticleHandleImp<Chaos::FReal, 3, true>* RigidParticle = ParticleHandlePtr->CastToRigidParticle())
	{
		WrappedParticleData.ParticleDynamics.CopyFrom(*RigidParticle);
		WrappedParticleData.ParticleDynamicsMisc.CopyFrom(*RigidParticle);
		WrappedParticleData.ParticleMassProps.CopyFrom(*RigidParticle);
	}
	return MoveTemp(WrappedParticleData);
}
#endif //WITH_CHAOS_VISUAL_DEBUGGER
