// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
#include "PhysicsSettingsEnums.h"

namespace physx
{
	class PxMaterial;
}

namespace ImmediatePhysics_PhysX
{
	struct FMaterial
	{
		FMaterial()
			: StaticFriction(0.7f)
			, DynamicFriction(0.7f)
			, Restitution(0.3f)
		{
		}

		FMaterial(physx::PxMaterial* InPxMaterial);

		float StaticFriction;
		float DynamicFriction;
		float Restitution;

		EFrictionCombineMode::Type FrictionCombineMode;
		EFrictionCombineMode::Type RestitutionCombineMode;

		static FMaterial Default;
	};
}

#endif // WITH_PHYSX