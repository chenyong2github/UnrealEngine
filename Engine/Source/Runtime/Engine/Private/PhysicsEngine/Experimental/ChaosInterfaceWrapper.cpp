// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosInterfaceWrapper.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/ParticleHandle.h"
#include "PhysxUserData.h"


namespace ChaosInterface
{
	FBodyInstance* GetUserData(const Chaos::TGeometryParticle<float,3>& Actor)
	{
		void* UserData = Actor.UserData();
		return UserData ? FPhysxUserData::Get<FBodyInstance>(Actor.UserData()) : nullptr;
	}

	UPhysicalMaterial* GetUserData(const FPhysTypeDummy& Material)
	{
		return nullptr;
	}

#if WITH_CHAOS
	FScopedSceneReadLock::FScopedSceneReadLock(FPhysScene_ChaosInterface& SceneIn)
		: Scene(SceneIn.GetScene())
	{
		Scene.ExternalDataLock.ReadLock();
	}

	FScopedSceneReadLock::~FScopedSceneReadLock()
	{
		Scene.ExternalDataLock.ReadUnlock();
	}
#endif
}
