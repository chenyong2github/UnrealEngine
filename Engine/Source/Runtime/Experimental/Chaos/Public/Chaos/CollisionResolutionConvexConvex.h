// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/CollisionResolutionManifold.h"
#include "Chaos/Convex.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/Transform.h"


namespace Chaos
{

template<class T, int d>
class CHAOS_API CollisionResolutionConvexConvex
{
public:
	using FConvex = TConvex<T,d>;
	using FRigidTransform = TRigidTransform<T,d>;
	using FRigidBodyContactConstraint = TRigidBodyContactConstraint<T, d>;
	using FGeometryParticleHandle = TGeometryParticleHandle<T, d>;

	/*
	* Build a constraint manifold for the contact shape pair. 
	*/
	static void ConstructConvexConvexConstraints(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const float Thickness,
		FRigidBodyContactConstraint& ConstraintBuffer);

	/*
	* Update the contact manifold.
	*/
	static void UpdateConvexConvexConstraint(const FImplicitObject& A, const FRigidTransform& ATM, const FImplicitObject& B, FRigidTransform BTM, float Thickness, 
		FRigidBodyContactConstraint& Constraints);


};

}
