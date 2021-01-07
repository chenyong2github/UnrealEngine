// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Engine/World.h"
	

AIMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogManagerInstanceTracker, Warning, All);

template<typename FManager>
struct TDefaultManagerInstanceTracker
{
	TMap<UWorld*, FManager*> WorldToInstanceMap;
	bool bCreateIfMissing = false;

	TDefaultManagerInstanceTracker()
	{
		FWorldDelegates::OnPostWorldCleanup.AddRaw(this, &TDefaultManagerInstanceTracker::OnPostWorldCleanup);
	}

	FManager* GetManagerInstance(UWorld& World)
	{
		FManager** FoundInstance = WorldToInstanceMap.Find(&World);
		if (FoundInstance)
		{
			return *FoundInstance;
		}
		else if (World.bIsTearingDown)
		{
			return nullptr;
		}

		FManager* NewInstance = nullptr;
		if (bCreateIfMissing)
		{
			NewInstance = NewObject<FManager>(&World);
			NewInstance->AddToRoot();
			WorldToInstanceMap.Add(&World, NewInstance);
		}
		else
		{
			UE_LOG(LogManagerInstanceTracker, Warning
				, TEXT("Calling FManager::GetCurrent while no instance for world %s has been created, and bCreateIfMissing == false")
				, *World.GetName());
		}

		return NewInstance;
	}

	void OnPostWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
	{
		FManager** FoundInstance = WorldToInstanceMap.Find(World);
		if (FoundInstance)
		{
			(*FoundInstance)->RemoveFromRoot();
			WorldToInstanceMap.Remove(World);
		}
	}
};
