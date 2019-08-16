// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ImmediatePhysicsShape_PhysX.h"

namespace ImmediatePhysics_PhysX
{

	/** Holds geometry data*/
	struct FActor
	{
		FActor()
			: UserData(nullptr)
		{}

		TArray<FShape> Shapes;
		void* UserData;

		/** Create geometry data for the entity */
		void CreateGeometry(PxRigidActor* RigidActor, const PxTransform& ActorToBodyTM);
		bool AddShape(PxShape* InShape);

		/** Ensures all the geometry data has been properly freed */
		void TerminateGeometry();
	};

}

#endif // WITH_PHYSX