// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/ImplicitObjectScaled.h"	// Cannot fwd declare scaled implicit

namespace Chaos
{
	class FContactPoint;

	extern CHAOS_API FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const FImplicitObject& Convex, const FRigidTransform3& ConvexTransform);

	extern CHAOS_API FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform);
	extern CHAOS_API FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform);
	extern CHAOS_API FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform);

	extern CHAOS_API FContactPoint ConvexCapsuleContactPoint(const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform, const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform);
	extern CHAOS_API FContactPoint ConvexCapsuleContactPoint(const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform);
	extern CHAOS_API FContactPoint ConvexCapsuleContactPoint(const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform);
}
