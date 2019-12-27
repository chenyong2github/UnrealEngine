// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsDeclares_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsShared/ImmediatePhysicsCore.h"

#include "Chaos/Core.h"
#include "Chaos/ParticleHandleFwd.h"

namespace Chaos
{
	class FImplicitObject;
	class FPBDJointConstraintHandle;
	class FPBDJointConstraints;
	template<typename T, int D> struct TKinematicGeometryParticleParameters;
	template<typename T, int D> class TKinematicTarget;
	template<typename P> class TPBDConstraintIslandRule;
	template<typename T, int D> struct TPBDRigidParticleParameters;
	template<typename T, int D> class TPBDRigidsEvolutionGBF;
	template<typename T, int D> class TPBDRigidsSOAs;
	template<typename T, int D> class TPerShapeData;
}

namespace ImmediatePhysics_Chaos
{
	using FReal = Chaos::FReal;
	const int Dimensions = 3;

	using EActorType = ImmediatePhysics_Shared::EActorType;
	using EForceType = ImmediatePhysics_Shared::EForceType;

	using FKinematicTarget = Chaos::TKinematicTarget<FReal, Dimensions>;
}

struct FBodyInstance;
struct FConstraintInstance;

// Used to define out code that still has to be implemented to match PhysX
#define IMMEDIATEPHYSICS_CHAOS_TODO 0
