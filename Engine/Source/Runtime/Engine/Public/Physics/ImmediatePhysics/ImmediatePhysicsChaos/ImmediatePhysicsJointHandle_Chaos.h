// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsCore_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Vector.h"

#include "Engine/EngineTypes.h"

namespace ImmediatePhysics_Chaos
{
	/** handle associated with a physics joint. This is the proper way to read/write to the physics simulation */
	struct ENGINE_API FJointHandle
	{
	public:
		using FChaosConstraintContainer = Chaos::TPBDJointConstraints<FReal, Dimensions>;
		using FChaosConstraintHandle = typename Chaos::TPBDJointConstraintHandle<FReal, Dimensions>;

		FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* InActor1, FActorHandle* InActor2);
		~FJointHandle();

		FChaosConstraintHandle* GetConstraint();
		const FChaosConstraintHandle* GetConstraint() const;

		const Chaos::TVector<FActorHandle*, 2>& GetActorHandles();
		const Chaos::TVector<const FActorHandle*, 2>& GetActorHandles() const;

		void UpdateLevels();

	private:
		Chaos::TVector<FActorHandle*, 2> ActorHandles;
		FChaosConstraintContainer* Constraints;
		FChaosConstraintHandle* ConstraintHandle;
	};
}
