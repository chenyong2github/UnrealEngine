// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectTestTypes.h"

//----------------------------------------------------------------------//
// USmartObjectTestSubsystem
//----------------------------------------------------------------------//
void USmartObjectTestSubsystem::RebuildAndInitializeForTesting(const TSharedPtr<FMassEntityManager>& InEntityManager)
{
	UWorld& World = GetWorldRef();
	OnWorldComponentsUpdated(World);

	if (MainCollection == nullptr)
	{
		SpawnMissingCollection();
	}

	if (MainCollection != nullptr)
	{
		RebuildCollection(*MainCollection);
	}

	InitializeRuntime(InEntityManager);
}

void USmartObjectTestSubsystem::SpawnMissingCollection()
{
	if (IsValid(MainCollection))
	{
		return;
	}

	UWorld& World = GetWorldRef();	
	ASmartObjectCollection* LocalCollection = World.SpawnActor<ASmartObjectCollection>(ASmartObjectTestCollection::StaticClass());//, SpawnInfo);
	check(LocalCollection);

	RegisterCollection(*LocalCollection);
}

FMassEntityManager* USmartObjectTestSubsystem::GetEntityManagerForTesting()
{
	return EntityManager.Get();
}

void USmartObjectTestSubsystem::CleanupRuntime()
{
	Super::CleanupRuntime();

	if (IsValid(MainCollection))
	{
		MainCollection->Destroy();
		MainCollection = nullptr;
	}
}

//----------------------------------------------------------------------//
// ASmartObjectTestCollection
//----------------------------------------------------------------------//
ASmartObjectTestCollection::ASmartObjectTestCollection()
{
	bIgnoreLevelTesting = true;
}

bool ASmartObjectTestCollection::RegisterWithSubsystem(const FString & Context)
{
	return false;
}

bool ASmartObjectTestCollection::UnregisterWithSubsystem(const FString& Context)
{
	return false;
}
