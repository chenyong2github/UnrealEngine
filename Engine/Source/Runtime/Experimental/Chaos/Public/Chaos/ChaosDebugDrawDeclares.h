// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Misc/Build.h"

#ifndef CHAOS_DEBUG_DRAW
#define CHAOS_DEBUG_DRAW !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

namespace Chaos
{
	template<typename T, int d>
	class TConstraintHandle;

	template<typename T, int d>
	class TPBD6DJointConstraintHandle;

	template<typename T, int d>
	class TPBD6DJointConstraints;

	template<typename T, int d>
	class TPBDCollisionConstraint;

	template<typename T, int d>
	class TPBDCollisionConstraintHandle;

	template<typename T, int d>
	class TPBDJointConstraintHandle;

	template<typename T, int d>
	class TPBDJointConstraints;

	template<typename T, int d>
	class TRigidTransform;



	template<typename T, int m, int n>
	class PMatrix;

	template<typename T, int d>
	class TRotation;

	template<typename T, int d>
	class TVector;
}
