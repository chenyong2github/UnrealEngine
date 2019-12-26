// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PhysicalMaterials.h"

namespace Chaos
{
	FChaosPhysicsMaterial* FMaterialHandle::Get() const
	{
		if(InnerHandle.IsValid())
		{
			return FPhysicalMaterialManager::Get().Resolve(InnerHandle);
		}
		return nullptr;
	}

	const FChaosPhysicsMaterial* FConstMaterialHandle::Get() const
	{
		if(InnerHandle.IsValid())
		{
			return FPhysicalMaterialManager::Get().Resolve(InnerHandle);
		}
		return nullptr;
	}

	FPhysicalMaterialManager::FPhysicalMaterialManager()
		: Materials(InitialCapacity)
	{

	}

	FPhysicalMaterialManager& FPhysicalMaterialManager::Get()
	{
		static FPhysicalMaterialManager Instance;
		return Instance;
	}

	FChaosPhysicsMaterial* FPhysicalMaterialManager::Resolve(FChaosMaterialHandle InHandle) const
	{
		return Materials.Get(InHandle);
	}

	const FChaosPhysicsMaterial* FPhysicalMaterialManager::Resolve(FChaosConstMaterialHandle InHandle) const
	{
		return Materials.Get(InHandle);
	}

	void FPhysicalMaterialManager::UpdateMaterial(FMaterialHandle InHandle)
	{
		check(IsInGameThread());

		OnMaterialUpdated.Broadcast(InHandle);
	}

	const Chaos::THandleArray<FChaosPhysicsMaterial>& FPhysicalMaterialManager::GetMasterMaterials() const
	{
		return Materials;
	}

	FMaterialHandle FPhysicalMaterialManager::Create()
	{
		check(IsInGameThread());
		FMaterialHandle OutHandle;
		OutHandle.InnerHandle = Materials.Create();

		OnMaterialCreated.Broadcast(OutHandle);

		return OutHandle;
	}


	void FPhysicalMaterialManager::Destroy(FMaterialHandle InHandle)
	{
		check(IsInGameThread());
		if(InHandle.InnerHandle.IsValid())
		{
			OnMaterialDestroyed.Broadcast(InHandle);

			Materials.Destroy(InHandle.InnerHandle);
		}
	}

}