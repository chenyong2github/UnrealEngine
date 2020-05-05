// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXLibrary.h"
#include "Library/DMXEntity.h"

#define LOCTEXT_NAMESPACE "DMXLibrary"

UDMXEntity* UDMXLibrary::GetOrCreateEntityObject(const FString& InName, TSubclassOf<UDMXEntity> DMXEntityClass)
{
	if (DMXEntityClass == nullptr)
	{
		DMXEntityClass = UDMXEntity::StaticClass(); 
	}

	if (!InName.IsEmpty())
	{
		for (UDMXEntity* Entity : Entities)
		{
			if (Entity != nullptr && Entity->IsA(DMXEntityClass) && Entity->GetDisplayName() == InName)
			{
				return Entity;
			}
		}
	}

	UDMXEntity* Entity = NewObject<UDMXEntity>(this, DMXEntityClass, NAME_None, RF_Transactional);
	Entity->SetName(InName);
	Entity->SetParentLibrary(this);
	Entities.Add(Entity);

	OnEntitiesUpdated.Broadcast(this);

	return Entity;
}

UDMXEntity* UDMXLibrary::FindEntity(const FString& InSearchName) const
{
	UDMXEntity*const* Entity = Entities.FindByPredicate([&InSearchName](const UDMXEntity* InEntity)->bool
		{
			return InEntity->GetDisplayName().Equals(InSearchName);
		});

	if (Entity != nullptr)
	{
		return *Entity;
	}
	return nullptr;
}

UDMXEntity* UDMXLibrary::FindEntity(const FGuid& Id)
{
	for (UDMXEntity* Entity : Entities)
	{
		if (Entity->GetID() == Id)
		{
			return Entity;
		}
	}

	return nullptr;
}

int32 UDMXLibrary::FindEntityIndex(UDMXEntity* InEntity) const
{
	return Entities.Find(InEntity);
}

void UDMXLibrary::AddEntity(UDMXEntity* InEntity)
{
	if (InEntity != nullptr)
	{
		Entities.Add(InEntity);
		InEntity->SetParentLibrary(this);

		// Check for unique Id
		for (UDMXEntity* Entity : Entities)
		{
			if (InEntity->GetID() != Entity->GetID())
			{
				InEntity->RefreshID();
				break;
			}
		}
	}
}

void UDMXLibrary::SetEntityIndex(UDMXEntity* InEntity, const int32 NewIndex)
{
	if (NewIndex < 0)
	{
		return; 
	}

	const int32&& OldIndex = Entities.Find(InEntity);
	if (OldIndex == INDEX_NONE || OldIndex == NewIndex)
	{
		return; 
	}

	if (NewIndex == OldIndex + 1)
	{
		return; 
	}

	// If elements are close to each other, just swap them. It's the fastest operation.
	if (NewIndex == OldIndex - 1)
	{
		Entities.SwapMemory(OldIndex, NewIndex);
	}
	else
	{
		if (NewIndex >= Entities.Num())
		{
			Entities.RemoveAt(OldIndex, 1, false);
			Entities.Add(InEntity);
			return;
		}

		// We could use RemoveAt then Insert, but that would shift every Entity after OldIndex on RemoveAt
		// and then every Entity after NewEntityIndex for Insert. Two shifts of possibly many elements!
		// Instead, we just need to shift all entities between NewIndex and OldIndex. Still a potentially
		// huge shift, but likely smaller on most situations.

		if (NewIndex > OldIndex)
		{
			// Shifts DOWN 1 place all elements between the target indexes, as NewIndex is after all of them
			for (int32 EntityIndex = OldIndex; EntityIndex < NewIndex - 1; ++EntityIndex)
			{
				Entities[EntityIndex] = Entities[EntityIndex + 1];
			}
			Entities[NewIndex - 1] = InEntity;
		}
		else
		{
			// Shifts UP 1 place all elements between the target indexes, as NewIndex is before all of them
			for (int32 EntityIndex = OldIndex; EntityIndex > NewIndex; --EntityIndex)
			{
				Entities[EntityIndex] = Entities[EntityIndex - 1];
			}
			Entities[NewIndex] = InEntity;
		}
	}
}

void UDMXLibrary::RemoveEntity(const FString& EntityName)
{
	int32 EntityIndex = Entities.IndexOfByPredicate([&EntityName] (const UDMXEntity* Entity)->bool
		{
			return Entity->GetDisplayName().Equals(EntityName);
		});

	if (EntityIndex != INDEX_NONE)
	{
		Entities[EntityIndex]->SetParentLibrary(nullptr);
		Entities.RemoveAt(EntityIndex);
		OnEntitiesUpdated.Broadcast(this);
	}
}

void UDMXLibrary::RemoveAllEntities()
{
	for (UDMXEntity* Entity : Entities)
	{
		Entity->SetParentLibrary(nullptr);
	}
	Entities.Empty();
	OnEntitiesUpdated.Broadcast(this);
}

const TArray<UDMXEntity*>& UDMXLibrary::GetEntities() const
{
	return Entities;
}

TArray<UDMXEntity*> UDMXLibrary::GetEntitiesOfType(TSubclassOf<UDMXEntity> InEntityClass) const
{
	return Entities.FilterByPredicate([&InEntityClass](const UDMXEntity* Entity)
		{
			return Entity->IsA(InEntityClass);
		});
}

void UDMXLibrary::ForEachEntityOfTypeWithBreak(TSubclassOf<UDMXEntity> InEntityClass, TFunction<bool(UDMXEntity*)> Predicate) const
{
	for (UDMXEntity* Entity : Entities)
	{
		if (Entity != nullptr && Entity->IsA(InEntityClass))
		{
			if (!Predicate(Entity)) 
			{ 
				break; 
			}
		}
	}
}

void UDMXLibrary::ForEachEntityOfType(TSubclassOf<UDMXEntity> InEntityClass, TFunction<void(UDMXEntity*)> Predicate) const
{
	for (UDMXEntity* Entity : Entities)
	{
		if (Entity != nullptr && Entity->IsA(InEntityClass))
		{
			Predicate(Entity);
		}
	}
}

FOnEntitiesUpdated& UDMXLibrary::GetOnEntitiesUpdated()
{
	return OnEntitiesUpdated;
}

void UDMXLibrary::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	if (DuplicateMode != EDuplicateMode::Normal)
	{
		return;
	}

	// Make sure all Entity children have this library as their parent
	// and refresh their ID
	for (UDMXEntity* Entity : Entities)
	{
		Entity->SetParentLibrary(this);
		Entity->RefreshID();
	}
}

#undef LOCTEXT_NAMESPACE
