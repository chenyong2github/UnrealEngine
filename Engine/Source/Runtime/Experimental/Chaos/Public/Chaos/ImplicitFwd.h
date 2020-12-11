// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Core.h"

namespace Chaos
{
	class FImplicitObject;
	class FConvex;

	template<class T, int d> class TBox;
	template<class T> class TCapsule;
	template<class T, int d> class TPlane;
	template<class T, int d> class TSphere;

	using FImplicitObject3 = FImplicitObject;
	using FImplicitBox3 = TBox<FReal, 3>;
	using FImplicitCapsule3 = TCapsule<FReal>;
	using FImplicitConvex3 = FConvex;
	using FImplicitPlane3 = TPlane<FReal, 3>;
	using FImplicitSphere3 = TSphere<FReal, 3>;
}