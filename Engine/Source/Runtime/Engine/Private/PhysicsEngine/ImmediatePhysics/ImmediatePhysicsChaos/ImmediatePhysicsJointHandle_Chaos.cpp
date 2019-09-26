// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"

#if INCLUDE_CHAOS

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/PBD6DJointConstraints.h"

#include "PhysicsEngine/ConstraintInstance.h"


namespace ImmediatePhysics_Chaos
{

	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: Constraints(InConstraints)
	{
		// BodyInstance/PhysX has the constraint locations in actor-space, but we need them in Centre-of-Mass space
		// @todo(ccaulfield): support CoM in Chaos particles
		FTransform ConstraintFrame1 = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1);
		FTransform ConstraintFrame2 = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame2);
		ConstraintFrame1.SetTranslation(ConstraintFrame1.GetTranslation() - Actor1->GetCoMTranslation());
		ConstraintFrame2.SetTranslation(ConstraintFrame2.GetTranslation() - Actor2->GetCoMTranslation());

		ConstraintHandle = Constraints->AddConstraint({ Actor1->ParticleHandle, Actor2->ParticleHandle }, { ConstraintFrame1, ConstraintFrame2 });
	}

	FJointHandle::~FJointHandle()
	{
		ConstraintHandle->RemoveConstraint();
	}

}

#endif
