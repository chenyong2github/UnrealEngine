// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Vector.h"
#include "Chaos/Matrix.h"
#include "Chaos/Rotation.h"
#include "Chaos/Transform.h"

namespace Chaos
{
	/**
	 * Common data types for the Chaos physics engine. Unless a specific
	 * precision of type is required most code should use these existing types
	 * (e.g. FVec3) to adapt to global changes in precision.
	 */
	using FReal = float;

	using FVec3 = TVector<FReal, 3>;
	using FMatrix33 = PMatrix<FReal, 3, 3>;
	using FRigidTransform3 = TRigidTransform<FReal, 3>;
	using FRotation3 = TRotation<FReal, 3>;


	template <typename T>
	using TVec3 = TVector<T, 3>;
}