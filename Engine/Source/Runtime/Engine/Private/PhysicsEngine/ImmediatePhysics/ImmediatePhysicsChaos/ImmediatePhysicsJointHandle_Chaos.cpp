// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"

#if INCLUDE_CHAOS

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/PBDJointConstraints.h"

#include "PhysicsEngine/ConstraintInstance.h"


namespace ImmediatePhysics_Chaos
{

	FJointHandle::FJointHandle(Chaos::TPBDJointConstraints<FReal, Dimensions>* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: Constraints(InConstraints)
		, ConstraintIndex(INDEX_NONE)
	{
		FVector JointLocationLocal = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1).GetLocation();
		FVector JointLocationWorld = Actor1->GetWorldTransform().TransformPosition(JointLocationLocal);
		ConstraintIndex = Constraints->AddConstraint({ Actor1->ParticleHandle, Actor2->ParticleHandle }, JointLocationWorld);

#if IMMEDIATEPHYSICS_CHAOS_TODO
		// Chaos constraint handles
		// Proper joint constraint
#endif
	}

	FJointHandle::~FJointHandle()
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
		// Remove joints
#endif
	}

}

#endif
