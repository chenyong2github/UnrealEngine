// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

namespace ChaosTest {

	/**/
	template<class T>
	void ImplicitCube();

	/**/
	template<class T>
	void ImplicitPlane();

	/**/
	template<class T>
	void ImplicitSphere();

	/**/
	template<class T>
	void ImplicitCylinder();

	/**/
	template<class T>
	void ImplicitTaperedCylinder();

	/**/
	template<class T>
	void ImplicitCapsule();

	/**/
	template <class T>
	void ImplicitScaled();

	/**/
	template <class T>
	void ImplicitTransformed();

	/**/
	template<class T>
	void ImplicitIntersection();

	/**/
	template<class T>
	void ImplicitUnion();

	/**/
	template<class T>
	void ImplicitLevelset();

	/**/
	template<class T>
	void RasterizationImplicit();

	template<class T>
	void RasterizationImplicitWithHole();

	template<class T>
	void ConvexHull();

	template<class T>
	void ConvexHull2();

	template<class T>
	void Simplify();

	template <class T>
	void ImplicitScaled2();
}