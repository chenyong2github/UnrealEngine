// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Vector.h"

namespace Chaos
{

	/** Specifies the type of work we should do*/
	enum class CHAOS_API ECollisionUpdateType
	{
		Any,	//stop if we have at least one deep penetration. Does not compute location or normal
		Deepest	//find the deepest penetration. Compute location and normal
	};

	/** Return value of the collision modification callback */
	enum class CHAOS_API ECollisionModifierResult
	{
		Unchanged,	/** No change to the collision */
		Modified,	/** Modified the collision, but want it to remain enabled */
		Disabled,	/** Collision should be disabled */
	};

	/** The shape types involved in a contact constraint. Used to look up the collision detection function */
	enum class CHAOS_API EContactShapesType
	{
		Unknown,
		SphereSphere,
		SphereCapsule,
		SphereBox,
		SphereConvex,
		SphereTriMesh,
		SphereHeightField,
		SpherePlane,
		CapsuleCapsule,
		CapsuleBox,
		CapsuleConvex,
		CapsuleTriMesh,
		CapsuleHeightField,
		BoxBox,
		BoxConvex,
		BoxTriMesh,
		BoxHeightField,
		BoxPlane,
		ConvexConvex,
		ConvexTriMesh,
		ConvexHeightField,
		GenericConvexConvex,
		LevelSetLevelSet,

		NumShapesTypes
	};

	enum class CHAOS_API EContactManifoldType
	{
		None,			// No manifold - run collision detection whenever we need latest contact
		OneShot,		// A manifold is created once and reused. The manifold consists of a plane attached to one shape and a set of points on the other
		Incremental,	// Run collision detection whenever we need the latest contact point, but keep track of and match contact points
	};

	//
	//
	//

	struct CHAOS_API FRigidBodyContactConstraintPGS
	{
		FRigidBodyContactConstraintPGS() : AccumulatedImpulse(0.f) {}
		TGeometryParticleHandle<FReal, 3>* Particle;
		TGeometryParticleHandle<FReal, 3>* Levelset;
		TArray<FVec3> Normal;
		TArray<FVec3> Location;
		TArray<FReal> Phi;
		FVec3 AccumulatedImpulse;
	};


	//TODO: move into a better forward declare location
	class FPBDCollisionConstraintHandle;

	/** Used to modify collision constraints via callback */
	class CHAOS_API FPBDCollisionConstraintHandleModification
	{
	public:
		FPBDCollisionConstraintHandleModification(FPBDCollisionConstraintHandle* InHandle)
			: Handle(InHandle)
			, Result(ECollisionModifierResult::Unchanged)
		{
		}

		void DisableConstraint() { Result = ECollisionModifierResult::Disabled; }

		//TODO: a better API would be to only return a mutable handle when this is set.
		//The problem is the current callback logic makes this cumbersome
		void ModifyConstraint()
		{
			Result = ECollisionModifierResult::Modified;
		}

		FPBDCollisionConstraintHandle* GetHandle() const { return Handle; }

		ECollisionModifierResult GetResult() const { return Result; }

	private:
		FPBDCollisionConstraintHandle* Handle;
		ECollisionModifierResult Result;
	};

	using FCollisionModifierCallback = TFunction<void(const TArrayView<FPBDCollisionConstraintHandleModification>& Handle)>;
}
