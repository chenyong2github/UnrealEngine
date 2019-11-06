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
	typedef TConvex<T,d> FConvex;
	typedef TRigidTransform<T,d> FRigidTransform;
	typedef TRigidBodyContactConstraint<T,d> FRigidBodyContactConstraint;
	typedef TCollisionResolutionManifold<T,d> FCollisionResolutionManifold;
	typedef TGeometryParticleHandle<T,d> FGeometryParticleHandle;

	/*
	* Build a constraint manifold for the contact shape pair. 
	*/
	static void ConstructConvexConvexConstraints(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const float Thickness,
		TArray<FRigidBodyContactConstraint>& ConstraintBuffer, FCollisionResolutionManifold* Manifold = nullptr, float ManifoldScale = 0.5, int32 ManifoldSamples = 4);

	/*
	* Update the contact manifold.
	*/
	static void UpdateConvexConvexConstraint(const FImplicitObject& A, const FRigidTransform& ATM, const FImplicitObject& B, FRigidTransform BTM, float Thickness, 
		FRigidBodyContactConstraint* Constraints, int32 NumConstraints, FCollisionResolutionManifold* Manifold, float ManifoldScale = 0.5, int32 ManifoldSamples = 4);


};

}
