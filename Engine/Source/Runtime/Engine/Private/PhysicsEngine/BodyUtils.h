// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "PhysicsInterfaceDeclaresCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "extensions/PxMassProperties.h"
#endif

struct FBodyInstance; 

#if WITH_CHAOS
namespace Chaos
{
	template<class T, int d>
	struct TMassProperties;

	using FShapesArray = TArray<TUniquePtr<FPerShapeData>, TInlineAllocator<1>>;
}
#endif

/**
 * Utility methods for use by BodyInstance and ImmediatePhysics
 */
namespace BodyUtils
{
#if WITH_CHAOS

	/** 
	 * Computes and adds the mass properties (inertia, com, etc...) based on the mass settings of the body instance. 
	 * Note: this includes a call to ModifyMassProperties, so the BodyInstance modifiers will be included in the calculation.
	 */
	Chaos::TMassProperties<float, 3> ComputeMassProperties(const FBodyInstance* OwningBodyInstance, const TArray<FPhysicsShapeHandle>& Shapes, const FTransform& MassModifierTransform, const bool bInertaScaleIncludeMass = false);
	Chaos::TMassProperties<float, 3> ComputeMassProperties(const FBodyInstance* OwningBodyInstance, const Chaos::FShapesArray& Shapes, const TArray<bool>& bContributesToMass, const FTransform& MassModifierTransform, const bool bInertaScaleIncludeMass = false);

#elif PHYSICS_INTERFACE_PHYSX
	
	/** Computes and adds the mass properties (inertia, com, etc...) based on the mass settings of the body instance. */
	physx::PxMassProperties ComputeMassProperties(const FBodyInstance* OwningBodyInstance, TArray<FPhysicsShapeHandle> Shapes, const FTransform& MassModifierTransform, const bool bUnused = false);

#endif

}