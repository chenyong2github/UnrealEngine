// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectCollection.h"
#include "SmartObjectTypes.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectComponent.h"
#include "Engine/World.h"
#include "VisualLogger/VisualLogger.h"

//----------------------------------------------------------------------//
// FSmartObjectCollectionEntry 
//----------------------------------------------------------------------//
FSmartObjectCollectionEntry::FSmartObjectCollectionEntry(const FSmartObjectID& SmartObjectID, const USmartObjectComponent& SmartObjectComponent)
	: ID(SmartObjectID)
	, Path(&SmartObjectComponent)
{
}

USmartObjectComponent* FSmartObjectCollectionEntry::GetComponent() const
{
	return CastChecked<USmartObjectComponent>(Path.ResolveObject(), ECastCheckedType::NullAllowed);
}

FString FSmartObjectCollectionEntry::Describe() const
{
	return FString::Printf(TEXT("%s - %s"), *Path.ToString(), *ID.Describe());
}

//----------------------------------------------------------------------//
// ASmartObjectCollection 
//----------------------------------------------------------------------//
ASmartObjectCollection::ASmartObjectCollection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bNetLoadOnClient = false;
	SetCanBeDamaged(false);
}

void ASmartObjectCollection::PostLoad()
{
	// Handle Level load, PIE, SIE, game load, streaming.
	Super::PostLoad();
	RegisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
}

void ASmartObjectCollection::Destroyed()
{
	// Handle editor delete.
	UnregisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
	Super::Destroyed();
}

void ASmartObjectCollection::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Handle Level unload, PIE end, SIE end, game end.
	UnregisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
	Super::EndPlay(EndPlayReason);
}

void ASmartObjectCollection::PostActorCreated()
{
	// Register after being initially spawned.
	Super::PostActorCreated();
	RegisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
}

void ASmartObjectCollection::PreRegisterAllComponents()
{
	Super::PreRegisterAllComponents();

	// Handle UWorld::AddToWorld(), i.e. turning on level visibility
	if (const ULevel* Level = GetLevel())
	{
		// This function gets called in editor all the time, we're only interested the case where level is being added to world.
		if (Level->bIsAssociatingLevel)
		{
			RegisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
		}
	}
}

void ASmartObjectCollection::PostUnregisterAllComponents()
{
	// Handle UWorld::RemoveFromWorld(), i.e. turning off level visibility
	if (const ULevel* Level = GetLevel())
	{
		// This function gets called in editor all the time, we're only interested the case where level is being removed from world.
		if (Level->bIsDisassociatingLevel)
		{
			UnregisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
		}
	}

	Super::PostUnregisterAllComponents();
}

bool ASmartObjectCollection::RegisterWithSubsystem(FString Context)
{
	if (bRegistered)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("\'%s\' (0x%llx) %s - Failed: already registered"), *GetName(), UPTRINT(this), *Context);
		return false;
	}

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("\'%s\' (0x%llx) %s - Failed: ignoring default object"), *GetName(), UPTRINT(this), *Context);
		return false;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(GetWorld());
	if (SmartObjectSubsystem == nullptr)
	{
		// Collection might attempt to register before the subsystem is created but at creation it will gather all collection
		// and register them. For this reason we use a log instead of an error
		UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("\'%s\' (0x%llx) %s - Failed: unable to find smart object subsystem"), *GetName(), UPTRINT(this), *Context);
		return false;
	}

	SmartObjectSubsystem->RegisterCollection(*this);
	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("\'%s\' (0x%llx) %s - Succeeded"), *GetName(), UPTRINT(this), *Context);
	return true;
}

bool ASmartObjectCollection::UnregisterWithSubsystem(FString Context)
{
	if (!bRegistered)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("\'%s\' (0x%llx) %s - Failed: not registered"), *GetName(), UPTRINT(this), *Context);
		return false;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(GetWorld());
	if (SmartObjectSubsystem == nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("\'%s\' (0x%llx) %s - Failed: unable to find smart object subsystem"), *GetName(), UPTRINT(this), *Context);
		return false;
	}

	SmartObjectSubsystem->UnregisterCollection(*this);
	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("\'%s\' (0x%llx) %s - Succeeded"), *GetName(), UPTRINT(this), *Context);
	return true;
}

FSmartObjectID ASmartObjectCollection::AddSmartObject(USmartObjectComponent& SOComponent)
{
	ensureMsgf(!SOComponent.GetWorld()->IsGameWorld(),
		TEXT("Registration can't happen at runtime for loaded entities; they should be in the initial collection"));

	const FSoftObjectPath Path(&SOComponent);
	FSmartObjectID ID(GetTypeHash(Path));

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Adding SmartObject %s [%s] to collection"), *SOComponent.GetName(), *ID.Describe());
	SOComponent.SetRegisteredID(ID);
	CollectionEntries.Emplace(ID, SOComponent);
	RegisteredIdToObjectMap.Add(ID, Path);
	return ID;
}

void ASmartObjectCollection::RemoveSmartObject(USmartObjectComponent& SOComponent)
{
	ensureMsgf(!SOComponent.GetWorld()->IsGameWorld(),
		TEXT("Registration can't happen at runtime for loaded components; they should be in the initial collection"));

	FSmartObjectID ID = SOComponent.GetRegisteredID();
	if (!ID.IsValid())
	{
		return;
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Removing SmartObject %s [%s] from collection"), *SOComponent.GetName(), *ID.Describe());
	const int32 Index = CollectionEntries.IndexOfByPredicate(
		[&ID](const FSmartObjectCollectionEntry& Entry)
		{
			return Entry.GetID() == ID;
		});

	if (Index != INDEX_NONE)
	{
		CollectionEntries.RemoveAt(Index);
		RegisteredIdToObjectMap.Remove(ID);
	}

	SOComponent.SetRegisteredID(FSmartObjectID::Invalid);
}

USmartObjectComponent* ASmartObjectCollection::GetSmartObjectComponent(const FSmartObjectID& SmartObjectID) const
{
	// @todo SO: see if the map worth it or a search in the array can be enough
	const FSoftObjectPath* Path = RegisteredIdToObjectMap.Find(SmartObjectID);
	return Path != nullptr ? CastChecked<USmartObjectComponent>(Path->ResolveObject(), ECastCheckedType::NullAllowed) : nullptr;
}

TConstArrayView<FSmartObjectCollectionEntry> ASmartObjectCollection::GetEntries() const
{
	return CollectionEntries;
}

void ASmartObjectCollection::OnRegistered()
{
	bRegistered = true;
	
	for (const FSmartObjectCollectionEntry& Entry : CollectionEntries)
	{
		RegisteredIdToObjectMap.Add(Entry.ID, Entry.Path);
	}
}

void ASmartObjectCollection::OnUnregistered()
{
	bRegistered = false;
}

#if WITH_EDITOR
void ASmartObjectCollection::PostEditUndo()
{
	Super::PostEditUndo();

	if (IsPendingKillPending())
	{
		UnregisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
	}
	else
	{
		RegisterWithSubsystem(ANSI_TO_TCHAR(__FUNCTION__));
	}
}

void ASmartObjectCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(ASmartObjectCollection, bBuildOnDemand))
		{
			if (!bBuildOnDemand)
			{
				RebuildCollection();
			}
		}
	}
}

void ASmartObjectCollection::RebuildCollection()
{
	if (USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(GetWorld()))
	{
		SmartObjectSubsystem->RebuildCollection(*this);
	}
}

void ASmartObjectCollection::RebuildCollection(TConstArrayView<USmartObjectComponent*> Components)
{
	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("\'%s\' (0x%llx) Rebuilding collection"), *GetName(), UPTRINT(this));
	CollectionEntries.Reset(Components.Num());
	RegisteredIdToObjectMap.Empty(Components.Num());

	for (USmartObjectComponent* const Component : Components)
	{
		if (Component != nullptr)
		{
			AddSmartObject(*Component);
		}
	}
}
#endif // WITH_EDITOR
