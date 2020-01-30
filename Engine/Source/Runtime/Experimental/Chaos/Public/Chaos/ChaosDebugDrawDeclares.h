// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Declares.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Misc/Build.h"

#ifndef CHAOS_DEBUG_DRAW
#define CHAOS_DEBUG_DRAW !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

namespace Chaos
{
	class FConstraintHandle;

	template<typename T, int d>
	class TPBDCollisionConstraints;

	template<typename T, int d>
	class TPBDCollisionConstraintHandle;

	class FPBDJointConstraintHandle;

	class FPBDJointConstraints;
}
