// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

namespace ImmediatePhysics_PhysX
{

	struct FActorHandle;

	struct FJoint
	{
		FActorHandle* DynamicActor;
		FActorHandle* OtherActor;
	};

}

#endif // WITH_PHYSX