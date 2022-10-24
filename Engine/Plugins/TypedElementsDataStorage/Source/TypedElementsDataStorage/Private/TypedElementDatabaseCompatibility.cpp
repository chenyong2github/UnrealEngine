// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabaseCompatibility.h"

#include "MassActorSubsystem.h"

void UTypedElementDatabaseCompatibility::Initialize(ITypedElementDataStorageInterface* StorageInterface)
{
	checkf(StorageInterface, TEXT("Typed Element's Database compatibility manager is being initialized with an invalid storage target."));

	Storage = StorageInterface;
	Prepare();

	StorageInterface->OnCreation().AddUObject(this, &UTypedElementDatabaseCompatibility::Prepare);
	StorageInterface->OnDestruction().AddUObject(this, &UTypedElementDatabaseCompatibility::Reset);
	StorageInterface->OnUpdate().AddUObject(this, &UTypedElementDatabaseCompatibility::AddPendingActors);
}

void UTypedElementDatabaseCompatibility::Deinitialize()
{
	Reset();
}

void UTypedElementDatabaseCompatibility::AddCompatibleObject(AActor* Actor)
{
	// Registration is delayed for two reasons:
	//	1. Allows entity creation in a single batch rather than multiple individual additions.
	//	2. Provides an opportunity to filter out the actors that are created within MASS itself as those will already be registered.
	ActorsPendingRegistration.Add(Actor);
}

void UTypedElementDatabaseCompatibility::RemoveCompatibleObject(AActor* Actor)
{
	checkf(Storage, TEXT("Removing compatible objects is not supported before Typed Element's Database compatibility manager has been initialized."));

	// If there is no actor subsystem it means that the world has been destroyed, including the MASS instance,
	// so there's no references to clean up.
	if (Storage && ActorSubsystem && Storage->IsAvailable() && Actor->GetWorld() == ActorSubsystem->GetWorld())
	{
		FMassEntityHandle Entity = ActorSubsystem->GetEntityHandleFromActor(Actor);
		if (Entity.IsValid()) // If there's no entity it may have been deleted earlier, e.g. through an explicit delete.
		{
			auto ActorStore = Storage->GetColumn<FMassActorFragment>(Entity.AsNumber());
			if (ActorStore && !ActorStore->IsOwnedByMass()) // Only remove actors that were externally created.
			{
				ActorSubsystem->RemoveHandleForActor(Actor);
				Storage->RemoveRow(Entity.AsNumber());
			}
		}
	}
}

void UTypedElementDatabaseCompatibility::Prepare()
{
	ActorSubsystem = Storage->GetExternalSystem<UMassActorSubsystem>();

	CreateStandardArchetypes();
}

void UTypedElementDatabaseCompatibility::Reset()
{
	ActorSubsystem = nullptr;
}

void UTypedElementDatabaseCompatibility::CreateStandardArchetypes()
{
	StandardActorTable = Storage->RegisterTable(MakeArrayView(
		{
			FMassActorFragment::StaticStruct()
		}), FName("Editor_StandardActorTable"));
}

void UTypedElementDatabaseCompatibility::AddPendingActors()
{
	// Delay processing until the required systems are available by not clearing the pending actor list.
	if (ActorsPendingRegistration.Num() > 0 && Storage && Storage->IsAvailable() && ActorSubsystem)
	{
		UWorld* TargetWorld = ActorSubsystem->GetWorld();

		// Filter out the actors that are already registered or already destroyed. 
		// The most common case for this is actors created from within MASS.
		TWeakObjectPtr<AActor>* ActorBegin = ActorsPendingRegistration.GetData();
		TWeakObjectPtr<AActor>* ActorIt = ActorsPendingRegistration.GetData();
		TWeakObjectPtr<AActor>* ActorEnd = ActorBegin + ActorsPendingRegistration.Num();
		while (ActorIt != ActorEnd)
		{
			if (
				(*ActorIt).IsValid() && // If not valid then the actor has already been deleted again.
				(*ActorIt)->GetWorld() == TargetWorld && // If not equal than this is not an actor for the global data storage.
				!ActorSubsystem->GetEntityHandleFromActor((*ActorIt).Get()).IsValid() // If true, this actor is likely created within MASS.
				) 
			{
				++ActorIt;
			}
			else
			{
				// Don't shrink the registration array as the array will be reused with a variety of different actor counts.
				// If memory size becomes an issue it's better to resize the array once after this loop rather than within the
				// loop to avoid many resizes happening.
				constexpr bool bAllowToShrink = false;
				ActorsPendingRegistration.RemoveAtSwap(ActorIt - ActorBegin, 1, bAllowToShrink);
				--ActorEnd;
			}
		}

		if (ActorBegin != ActorEnd) // Check if the number of actors to add isn't zero.
		{
			// Add the remaining actors to the data storage.
			ActorIt = ActorsPendingRegistration.GetData();
			Storage->BatchAddRow(StandardActorTable, ActorsPendingRegistration.Num(), [this, &ActorIt, ActorEnd](TypedElementRowHandle Row)
				{
					auto ActorStore = Storage->GetColumn<FMassActorFragment>(Row);
					if (ActorStore)
					{
						constexpr bool bIsOwnedByMass = false;
						ActorStore->SetAndUpdateHandleMap(FMassEntityHandle::FromNumber(Row), ActorIt->Get(), bIsOwnedByMass);

						checkf(ActorIt < ActorEnd,
							TEXT("More (%i) entities were added than were requested (%i)."), ActorEnd - ActorIt, ActorsPendingRegistration.Num());
						++ActorIt;
					}
				});

			// Reset the container for next set of actors.
			ActorsPendingRegistration.Reset();
		}
	}
}
