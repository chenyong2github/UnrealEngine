// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"

namespace ChaosTest {

	/**/
	template<typename T>
	void SimplexLine();

	template<typename T>
	void SimplexTriangle();

	template <typename T>
	void SimplexTetrahedron();

	template <typename T>
	void GJKSphereSphereTest();

	template <typename T>
	void GJKSphereBoxTest();

	template <typename T>
	void GJKSphereCapsuleTest();

	template <typename T>
	void GJKSphereConvexTest();

	template <typename T>
	void GJKSphereScaledSphereTest();

	template <typename T>
	void GJKSphereSphereSweep();

	template <typename T>
	void GJKSphereBoxSweep();

	template <typename T>
	void GJKSphereCapsuleSweep();

	template <typename T>
	void GJKSphereConvexSweep();

	template <typename T>
	void GJKSphereScaledSphereSweep();

	template <typename T>
	void GJKSphereTransformedSphereSweep();

	template <typename T>
	void GJKBoxCapsuleSweep();

	template <typename T>
	void GJKBoxBoxSweep();

	template <typename T>
	void GJKCapsuleConvexInitialOverlapSweep();
}