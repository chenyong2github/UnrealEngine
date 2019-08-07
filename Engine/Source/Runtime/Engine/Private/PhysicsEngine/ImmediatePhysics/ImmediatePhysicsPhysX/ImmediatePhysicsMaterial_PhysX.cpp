// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ImmediatePhysicsMaterial_PhysX.h"

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"

namespace ImmediatePhysics_PhysX
{
	FMaterial::FMaterial(physx::PxMaterial* InPxMaterial)
		: StaticFriction(InPxMaterial->getStaticFriction())
		, DynamicFriction(InPxMaterial->getDynamicFriction())
		, Restitution(InPxMaterial->getRestitution())
		, FrictionCombineMode((EFrictionCombineMode::Type)InPxMaterial->getFrictionCombineMode())
		, RestitutionCombineMode((EFrictionCombineMode::Type)InPxMaterial->getRestitutionCombineMode())
	{
	}

	/** Default shape material. Created from the CDO of UPhysicalMaterial */
	FMaterial FMaterial::Default;
}

#endif // WITH_PHYSX
