// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsCore_Chaos.h"

#include "Engine/EngineTypes.h"

namespace ImmediatePhysics_Chaos
{
	/** handle associated with a physics joint. This is the proper way to read/write to the physics simulation */
	struct ENGINE_API FJointHandle
	{
	public:
		FJointHandle(Chaos::TPBDJointConstraints<FReal, Dimensions>* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2);
		~FJointHandle();

	private:
		Chaos::TPBDJointConstraints<FReal, Dimensions>* Constraints;
		int32 ConstraintIndex;
	};
}
#endif
