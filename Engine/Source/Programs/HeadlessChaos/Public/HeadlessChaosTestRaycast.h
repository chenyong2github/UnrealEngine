// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "HeadlessChaosTestUtility.h"

namespace ChaosTest {

	template<typename T>
	void SphereRaycast();

	template<typename T>
	void PlaneRaycast();

	template<typename T>
	void CylinderRaycast();

	template<typename T>
	void TaperedCylinderRaycast();

	template<typename T>
	void CapsuleRaycast();

	template<typename T>
	void TriangleRaycast();

	template<typename T>
	void BoxRaycast();

	template<typename T>
	void ScaledRaycast();

	template<typename T>
	void TransformedRaycast();

	template<typename T>
	void UnionRaycast();

	template<typename T>
	void IntersectionRaycast();
}