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
		LevelSetLevelSet,

		NumShapesTypes
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
	using FCollisionModifierCallback = TFunction<ECollisionModifierResult(FPBDCollisionConstraintHandle*)>;
}
