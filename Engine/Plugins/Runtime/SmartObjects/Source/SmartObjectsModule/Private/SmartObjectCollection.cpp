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
FSmartObjectCollectionEntry::FSmartObjectCollectionEntry(const FSmartObjectID& SmartObjectID, const USmartObjectComponent& SmartObjectComponent, const uint32 ConfigIndex)
	: ID(SmartObjectID)
	, Path(&SmartObjectComponent)
	, Transform(SmartObjectComponent.GetComponentTransform())
	, Bounds(SmartObjectComponent.GetSmartObjectBounds())
	, ConfigIdx(ConfigIndex)
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

bool ASmartObjectCollection::RegisterWithSubsystem(const FString& Context)
{
	if (bRegistered)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("'%s' %s - Failed: already registered"), *GetName(), *Context);
		return false;
	}

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("'%s' %s - Failed: ignoring default object"), *GetName(), *Context);
		return false;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(GetWorld());
	if (SmartObjectSubsystem == nullptr)
	{
		// Collection might attempt to register before the subsystem is created. At its initialization the subsystem gathers
		// all collections and registers them. For this reason we use a log instead of an error.
		UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("'%s' %s - Failed: unable to find smart object subsystem"), *GetName(), *Context);
		return false;
	}

	SmartObjectSubsystem->RegisterCollection(*this);
	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("'%s' %s - Succeeded"), *GetName(), *Context);
	return true;
}

bool ASmartObjectCollection::UnregisterWithSubsystem(const FString& Context)
{
	if (!bRegistered)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("'%s' %s - Failed: not registered"), *GetName(), *Context);
		return false;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(GetWorld());
	if (SmartObjectSubsystem == nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("'%s' %s - Failed: unable to find smart object subsystem"), *GetName(), *Context);
		return false;
	}

	SmartObjectSubsystem->UnregisterCollection(*this);
	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("'%s' %s - Succeeded"), *GetName(), *Context);
	return true;
}

bool ASmartObjectCollection::AddSmartObject(USmartObjectComponent& SOComponent)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("'%s' can't be registered to collection '%s': no associated world"),
			*GetNameSafe(SOComponent.GetOwner()), *GetName());
		return false;
	}

	const FSoftObjectPath ObjectPath = &SOComponent;
	FString AssetPathString = ObjectPath.GetAssetPathString();

	// We are not using asset path for partitioned world since they are not stable between editor and runtime.
	// SubPathString should be enough since all actors are part of the main level.
	if (World->IsPartitionedWorld())
	{
		AssetPathString.Reset();
	}
#if WITH_EDITOR
	else if (World->WorldType == EWorldType::PIE)
	{
		AssetPathString = UWorld::RemovePIEPrefix(ObjectPath.GetAssetPathString());
	}
#endif // WITH_EDITOR

	// Compute hash manually from strings since GetTypeHash(FSoftObjectPath) relies on a FName which implements run-dependent hash computations.
	FSmartObjectID ID = HashCombine(GetTypeHash(AssetPathString), GetTypeHash(ObjectPath.GetSubPathString()));
	SOComponent.SetRegisteredID(ID);

	const FSmartObjectCollectionEntry* ExistingEntry = CollectionEntries.FindByPredicate([ID](const FSmartObjectCollectionEntry& Entry)
	{
		return Entry.ID == ID;
	});

	if (ExistingEntry != nullptr)
	{
		UE_VLOG_UELOG(this, LogSmartObject, VeryVerbose, TEXT("'%s[ID=%s]' already registered to collection '%s'"),
			*GetNameSafe(SOComponent.GetOwner()), *ID.Describe(), *GetName());
		return false;
	}

	const AActor* ComponentOwner = SOComponent.GetOwner();
	const TSubclassOf<UObject> OwnerClass = ComponentOwner != nullptr ? ComponentOwner->GetClass() : SOComponent.GetClass();

	uint32 ConfigIndex = INDEX_NONE;
	if (const uint32* const ExistingConfigIndex = ConfigLookup.Find(OwnerClass))
	{
		ConfigIndex = *ExistingConfigIndex;
	}
	else
	{
		FObjectDuplicationParameters DuplicationParameters(&SOComponent, this);
		DuplicationParameters.DestName = *FString::Printf(TEXT("ConfigPool_%d_%s"), Configurations.Num(), *OwnerClass->GetName());

		// Ideally we would clone only the configuration but some inner instanced UObjects require a UObject parent in the chain.
		USmartObjectComponent* TemplateComponent = Cast<USmartObjectComponent>(StaticDuplicateObjectEx(DuplicationParameters));
		check(TemplateComponent != nullptr);

		// make sure the duplicated component is not considered blueprint since it can't be reconstructed
		TemplateComponent->CreationMethod = EComponentCreationMethod::Native;

		// break any link to another component
		TemplateComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);

		UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Adding config '%s' to the shared pool"), *TemplateComponent->GetName());

		ConfigIndex = Configurations.Add(TemplateComponent);
		ConfigLookup.Add(OwnerClass, ConfigIndex);
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Adding '%s[ID=%s]' to collection '%s'"), *GetNameSafe(SOComponent.GetOwner()), *ID.Describe(), *GetName());
	CollectionEntries.Emplace(ID, SOComponent, ConfigIndex);
	RegisteredIdToObjectMap.Add(ID, ObjectPath);
	return true;
}

bool ASmartObjectCollection::RemoveSmartObject(USmartObjectComponent& SOComponent)
{
	FSmartObjectID ID = SOComponent.GetRegisteredID();
	if (!ID.IsValid())
	{
		return false;
	}

	UE_VLOG_UELOG(this, LogSmartObject, Verbose, TEXT("Removing '%s[ID=%s]' from collection '%s'"), *GetNameSafe(SOComponent.GetOwner()), *ID.Describe(), *GetName());
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

	return Index != INDEX_NONE;
}

USmartObjectComponent* ASmartObjectCollection::GetSmartObjectComponent(const FSmartObjectID& SmartObjectID) const
{
	const FSoftObjectPath* Path = RegisteredIdToObjectMap.Find(SmartObjectID);
	return Path != nullptr ? CastChecked<USmartObjectComponent>(Path->ResolveObject(), ECastCheckedType::NullAllowed) : nullptr;
}

const FSmartObjectConfig* ASmartObjectCollection::GetConfigForEntry(const FSmartObjectCollectionEntry& Entry) const
{
	const bool bIsValidIndex = Configurations.IsValidIndex(Entry.GetConfigIndex());
	if (!bIsValidIndex)
	{
		UE_VLOG_UELOG(this, LogSmartObject, Error, TEXT("Using invalid index (%d) to retrieve configuration from collection '%s'"), Entry.GetConfigIndex(), *GetName());	
		return nullptr;
	}

	const USmartObjectComponent* Component = Configurations[Entry.GetConfigIndex()];
	ensureMsgf(Component != nullptr, TEXT("Collection is expected to contain only valid configuration entries"));
	return Component != nullptr ? &(Component->GetConfig()) : nullptr;
}

void ASmartObjectCollection::OnRegistered()
{
	bRegistered = true;
}

void ASmartObjectCollection::OnUnregistered()
{
	bRegistered = false;
}

void ASmartObjectCollection::ValidateConfigs()
{
	for (const USmartObjectComponent* Component : Configurations)
	{
		if (ensureMsgf(Component != nullptr, TEXT("Collection is expected to contain only valid configuration entries")))
		{
			Component->GetConfig().Validate();
		}
	}
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

		// Dirty package since this is an explicit user action
		MarkPackageDirty();
	}
}

void ASmartObjectCollection::RebuildCollection(const TConstArrayView<USmartObjectComponent*> Components)
{
	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("Rebuilding collection '%s' from component list"), *GetName());
	CollectionEntries.Reset(Components.Num());
	RegisteredIdToObjectMap.Empty(Components.Num());

	ConfigLookup.Reset();
	Configurations.Reset();

	for (USmartObjectComponent* const Component : Components)
	{
		if (Component != nullptr)
		{
			AddSmartObject(*Component);
		}
	}

	CollectionEntries.Shrink();
	RegisteredIdToObjectMap.Shrink();
	ConfigLookup.Shrink();
	Configurations.Shrink();
}

void ASmartObjectCollection::ResetCollection()
{
	UE_VLOG_UELOG(this, LogSmartObject, Log, TEXT("Reseting collection '%s'"), *GetName());

	CollectionEntries.Reset();
	RegisteredIdToObjectMap.Reset();
	ConfigLookup.Reset();
	Configurations.Reset();
}
#endif // WITH_EDITOR
