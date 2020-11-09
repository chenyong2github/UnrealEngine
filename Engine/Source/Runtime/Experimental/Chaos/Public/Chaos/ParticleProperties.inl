// Copyright Epic Games, Inc. All Rights Reserved.

#undef PARTICLE_PROPERTY_CHECKED
#if CHAOS_CHECKED
#define PARTICLE_PROPERTY_CHECKED(x, Type) PARTICLE_PROPERTY(x, Type)
#else
#define PARTICLE_PROPERTY_CHECKED(x, Type)
#endif

PARTICLE_PROPERTY(XR, FParticlePositionRotation)
PARTICLE_PROPERTY(Velocities, FParticleVelocities)
PARTICLE_PROPERTY(Dynamics, FParticleDynamics)
PARTICLE_PROPERTY(DynamicMisc, FParticleDynamicMisc)
PARTICLE_PROPERTY(NonFrequentData, FParticleNonFrequentData)
PARTICLE_PROPERTY(MassProps, FParticleMassProps)
PARTICLE_PROPERTY(KinematicTarget,FKinematicTarget)