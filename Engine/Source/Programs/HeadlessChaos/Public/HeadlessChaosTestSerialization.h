// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "HeadlessChaosTestUtility.h"

namespace ChaosTest {

	template<typename T>
	void SimpleObjectsSerialization();

	template <typename T>
	void SharedObjectsSerialization();

	template<typename T>
	void GraphSerialization();

	template<typename T>
	void ObjectUnionSerialization();

	template <typename T>
	void ParticleSerialization();

	template <typename T>
	void BVHSerialization();

	template<class T>
	void RigidParticlesSerialization();

	template<class T>
	void BVHParticlesSerialization();

	template<class T>
	void GeometryCollectionSerialization();

	void EvolutionPerfHarness();

}