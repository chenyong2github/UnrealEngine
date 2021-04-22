// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/ImplicitObjectScaled.h"	// Cannot fwd declare scaled implicit

namespace Chaos
{
	class FContactPoint;

	extern CHAOS_API FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const FImplicitObject& Convex, const FRigidTransform3& ConvexTransform);

	extern CHAOS_API FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform);
	extern CHAOS_API FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform);
	extern CHAOS_API FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform);

	extern CHAOS_API FContactPoint ConvexSphereContactPoint(const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform, const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform);
	extern CHAOS_API FContactPoint ConvexSphereContactPoint(const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform);
	extern CHAOS_API FContactPoint ConvexSphereContactPoint(const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform);
}
