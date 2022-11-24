// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectTestTypes.h"

//----------------------------------------------------------------------//
// USmartObjectTestSubsystem
//----------------------------------------------------------------------//
void USmartObjectTestSubsystem::RebuildAndInitializeForTesting(const TSharedPtr<FMassEntityManager>& InEntityManager)
{
	UWorld& World = GetWorldRef();
	OnWorldComponentsUpdated(World);

	InitializeRuntime(InEntityManager);
}

FMassEntityManager* USmartObjectTestSubsystem::GetEntityManagerForTesting()
{
	return EntityManager.Get();
}

//----------------------------------------------------------------------//
// ASmartObjectTestCollection
//----------------------------------------------------------------------//
bool ASmartObjectTestCollection::RegisterWithSubsystem(const FString & Context)
{
	return false;
}

bool ASmartObjectTestCollection::UnregisterWithSubsystem(const FString& Context)
{
	return false;
}
