// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosInterfaceWrapper.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/ParticleHandle.h"
#include "PhysxUserData.h"


namespace ChaosInterface
{
	FBodyInstance* GetUserData(const Chaos::TGeometryParticle<float,3>& Actor)
	{
		void* UserData = Actor.UserData();
		return UserData ? FChaosUserData::Get<FBodyInstance>(Actor.UserData()) : nullptr;
	}

	UPhysicalMaterial* GetUserData(const Chaos::FChaosPhysicsMaterial& Material)
	{
		void* UserData = Material.UserData;
		return UserData ? FChaosUserData::Get<UPhysicalMaterial>(UserData) : nullptr;
	}

#if WITH_CHAOS
	FScopedSceneReadLock::FScopedSceneReadLock(FPhysScene_Chaos& SceneIn)
		: Scene(SceneIn)
	{
		Scene.ExternalDataLock.ReadLock();
	}

	FScopedSceneReadLock::~FScopedSceneReadLock()
	{
		Scene.ExternalDataLock.ReadUnlock();
	}
#endif
}
