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

	template<class T>
	void LevelsetConstraint();

	template<class T>
	void LevelsetConstraintGJK();
		
	template<class T>
	void CollisionBoxPlane();

	template<class T>
	void CollisionConvexConvex();

	template<class T>
	void CollisionBoxPlaneZeroResitution();

	template<class T>
	void CollisionBoxPlaneRestitution();

	template<class T>
	void CollisionBoxToStaticBox();

	template<class T>
	void CollisionPGS();

	template<class T>
	void CollisionPGS2();

	template<class T>
	void CollisionBroadphaseVelocityExpansion();

}