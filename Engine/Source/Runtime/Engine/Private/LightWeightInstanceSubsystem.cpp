// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/LightWeightInstanceSubsystem.h"
#include "GameFramework/LightWeightInstanceManager.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogLightWeightInstance);

TSharedPtr<FLightWeightInstanceSubsystem> FLightWeightInstanceSubsystem::LWISubsystem;

int32 FLightWeightInstanceSubsystem::GetManagerIndex(const ALightWeightInstanceManager* Manager) const
{
	for (int32 Idx = 0; Idx < LWInstanceManagers.Num(); ++Idx)
	{
		if (LWInstanceManagers[Idx] == Manager)
		{
			return Idx;
		}
	}

	return INDEX_NONE;
}

const ALightWeightInstanceManager* FLightWeightInstanceSubsystem::GetManagerAt(int32 Index) const
{
	return Index < LWInstanceManagers.Num() ? LWInstanceManagers[Index] : nullptr;
}

ALightWeightInstanceManager* FLightWeightInstanceSubsystem::FindLightWeightInstanceManager(const FActorInstanceHandle& Handle) const
{
	if (Handle.ManagerIndex != INDEX_NONE)
	{
		if (ensure(Handle.ManagerIndex < LWInstanceManagers.Num()))
		{
			return LWInstanceManagers[Handle.ManagerIndex];
		}
	}

	if (Handle.Actor.IsValid())
	{
		for (ALightWeightInstanceManager* LWInstance : LWInstanceManagers)
		{
			if (Handle.Actor->GetClass() == LWInstance->GetRepresentedClass() && Handle.Actor->GetLevel() == LWInstance->GetLevel())
			{
				return LWInstance;
			}
		}
	}

	return nullptr;
}

ALightWeightInstanceManager* FLightWeightInstanceSubsystem::FindLightWeightInstanceManager(UClass* ActorClass, ULevel* Level) const
{
	if (ActorClass == nullptr || Level == nullptr)
	{
		return nullptr;
	}

	// see if we already have a match
	for (ALightWeightInstanceManager* Instance : LWInstanceManagers)
	{
		if (Instance->GetRepresentedClass() == ActorClass && Instance->GetLevel() == Level)
		{
			return Instance;
		}
	}

	return nullptr;
}

ALightWeightInstanceManager* FLightWeightInstanceSubsystem::FindOrAddLightWeightInstanceManager(UClass* ActorClass, ULevel* Level)
{
	if (ActorClass == nullptr || Level == nullptr)
	{
		return nullptr;
	}
	
	// see if we already have a match
	for (ALightWeightInstanceManager* Instance : LWInstanceManagers)
	{
		if (Instance->GetRepresentedClass() == ActorClass && Instance->GetLevel() == Level)
		{
			return Instance;
		}
	}

	// we didn't find a match so we should add one.

	// Find the best base class to start from
	UClass* BestMatchingClass = FindBestInstanceManagerClass(ActorClass);
	if (BestMatchingClass == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = Level;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags = RF_Transactional;

	ALightWeightInstanceManager* NewInstance = Level->GetWorld()->SpawnActor<ALightWeightInstanceManager>(BestMatchingClass, FTransform::Identity, SpawnParams);
	ULevel* InstanceLvl = NewInstance->GetLevel();
	NewInstance->SetRepresentedClass(ActorClass);

	check(LWInstanceManagers.Find(NewInstance) != INDEX_NONE);

	return NewInstance;
}

UClass* FLightWeightInstanceSubsystem::FindBestInstanceManagerClass(const UClass* InActorClass)
{
	// Get every light weight instance class
	// FRED_TODO: we need to search through unloaded blueprints as well
	// FRED_TODO: this should be done once and cached. In the editor we can add a listener for when new BP classes are added
	TArray<UClass*> ManagerClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(ALightWeightInstanceManager::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			// Skip SKEL and REINST classes.
			if (It->GetName().StartsWith(TEXT("SKEL_")) || It->GetName().StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			ManagerClasses.Add(*It);
		}
	}

	// Figure out which one is the closest fit for InActorClass
	UClass* BestManagerClass = nullptr;
	int32 BestDistance = INT_MAX;
	for (UClass* ManagerClass : ManagerClasses)
	{
		if (ManagerClass->GetDefaultObject<ALightWeightInstanceManager>()->DoesAcceptClass(InActorClass))
		{
			const UClass* HandledClass = ManagerClass->GetDefaultObject<ALightWeightInstanceManager>()->GetRepresentedClass();
			if (!HandledClass)
			{
				HandledClass = ManagerClass->GetDefaultObject<ALightWeightInstanceManager>()->GetAcceptedClass();
			}
			if (InActorClass == HandledClass)
			{
				BestManagerClass = ManagerClass;
				break;
			}
			const UClass* ActorClass = InActorClass;
			int32 Distance = 0;
			while (ActorClass && ActorClass != HandledClass)
			{
				++Distance;
				ActorClass = ActorClass->GetSuperClass();
			}
			if (ActorClass && Distance < BestDistance)
			{
				BestDistance = Distance;
				BestManagerClass = ManagerClass;
			}
		}
	}

	return BestManagerClass;
}

AActor* FLightWeightInstanceSubsystem::GetActor(const FActorInstanceHandle& Handle)
{
	// if the actor is valid return it
	if (Handle.Actor.IsValid())
	{
		return Handle.Actor.Get();
	}

	if (ALightWeightInstanceManager* LWIManager = FindLightWeightInstanceManager(Handle))
	{
		return LWIManager->GetActorFromHandle(Handle);
	}

	return nullptr;
}

AActor* FLightWeightInstanceSubsystem::GetActor_NoCreate(const FActorInstanceHandle& Handle) const
{
	return Handle.Actor.Get();
}

UClass* FLightWeightInstanceSubsystem::GetActorClass(const FActorInstanceHandle& Handle)
{
	if (Handle.Actor.IsValid())
	{
		return Handle.Actor->StaticClass();
	}

	if (ALightWeightInstanceManager* LWIManager = FindLightWeightInstanceManager(Handle))
	{
		return LWIManager->GetRepresentedClass();
	}

	return nullptr;
}

FVector FLightWeightInstanceSubsystem::GetLocation(const FActorInstanceHandle& Handle)
{
	ensure(Handle.IsValid());

	if (Handle.Actor.IsValid())
	{
		return Handle.Actor->GetActorLocation();
	}

	if (ALightWeightInstanceManager* InstanceManager = FindLightWeightInstanceManager(Handle))
	{
		return InstanceManager->GetLocation(Handle);
	}

	return FVector::ZeroVector;
}

FString FLightWeightInstanceSubsystem::GetName(const FActorInstanceHandle& Handle)
{
	ensure(Handle.IsValid());

	if (Handle.Actor.IsValid())
	{
		return Handle.Actor->GetName();
	}

	if (ALightWeightInstanceManager* InstanceManager = FindLightWeightInstanceManager(Handle))
	{
		return InstanceManager->GetName(Handle);
	}

	return TEXT("None");
}

ULevel* FLightWeightInstanceSubsystem::GetLevel(const FActorInstanceHandle& Handle)
{
	ensure(Handle.IsValid());

	if (Handle.Actor.IsValid())
	{
		return Handle.Actor->GetLevel();
	}

	if (ALightWeightInstanceManager* InstanceManager = FindLightWeightInstanceManager(Handle))
	{
		return InstanceManager->GetLevel();
	}

	return nullptr;
}

bool FLightWeightInstanceSubsystem::IsInLevel(const FActorInstanceHandle& Handle, const ULevel* InLevel)
{
	ensure(Handle.IsValid());

	if (Handle.Actor.IsValid())
	{
		return Handle.Actor->IsInLevel(InLevel);
	}

	if (ALightWeightInstanceManager* InstanceManager = FindLightWeightInstanceManager(Handle))
	{
		return InstanceManager->GetLevel() == InLevel;
	}

	return false;
}