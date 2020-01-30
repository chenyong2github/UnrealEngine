// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsPhysX/ResourceManager_PhysX.h"

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

namespace ImmediatePhysics_PhysX
{

	FSharedResourceManager FSharedResourceManager::Instance;

	FResourceHandle FSharedResourceManager::CreateMaterial()
	{
		int32 Index = Materials.Add(TResourceWithId<FMaterial>());
		int32 Id = Materials[Index].Id;

		return FResourceHandle(EResourceType::Material, Index, Id);
	}

	void FSharedResourceManager::ReleaseMaterial(int32 InIndex)
	{
		Materials.RemoveAt(InIndex);
	}

	int32 FSharedResourceManager::GetMaterialId(int32 InIndex)
	{
		if (InIndex != INDEX_NONE && Materials.IsAllocated(InIndex))
		{
			return Materials[InIndex].Id;
		}

		return INDEX_NONE;
	}

	FMaterial* FSharedResourceManager::GetMaterial(int32 InIndex)
	{
		if (InIndex != INDEX_NONE && Materials.IsAllocated(InIndex))
		{
			return &Materials[InIndex].Resource;
		}

		return nullptr;
	}

	FRWLock& FSharedResourceManager::GetLockObject()
	{
		return ResourceLock;
	}

}

#endif // WITH_PHYSX