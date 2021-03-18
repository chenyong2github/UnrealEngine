// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXLibrary.h"

#include "DMXRuntimeLog.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXPortManager.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixturePatch.h"


#define LOCTEXT_NAMESPACE "DMXLibrary"

void UDMXLibrary::PostLoad()
{
	Super::PostLoad();

	TArray<UDMXEntity*> CachedEntities = Entities;
	for (UDMXEntity* Entity : CachedEntities)
	{
		// Clean out controllers that were in the entity array before 4.27
		if (UDMXEntityController* VoidController = Cast<UDMXEntityController>(Entity))
		{
			UE_LOG(LogDMXRuntime, Warning, TEXT("UE4.27: Removed obsolete Controllers from DMXLibrary %s. Please setup ports for the library and resave the asset."), *GetName());
			Modify();
			Entities.Remove(VoidController);
		}

		// From hereon all entities have to be valid. We should never enter this statement, ever.
		if (!ensure(Entity))
		{
			UE_LOG(LogDMXRuntime, Warning, TEXT("Invalid Entity found in Library %s. Please resave the library."), *GetName());
			Modify();
			Entities.Remove(Entity);
		}
	}

	// Update ports
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UpdatePorts();
	}
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
	TArray<UDMXEntity*> ValidEntities;
	for (UDMXEntity* Entity : Entities)
	{
		// Entity could be null
		if (ensure(Entity))
		{
			Entity->SetParentLibrary(this);
			Entity->RefreshID();
			ValidEntities.Add(Entity);
		}
	}

	// duplicate only valid entities
	Entities = ValidEntities;
}

#if WITH_EDITOR
void UDMXLibrary::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	Super::PostEditChangeProperty(PropertyChangedChainEvent);

	FName PropertyName = PropertyChangedChainEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXLibrary, InputPortReferences))
	{
		if(InputPortReferences.Num() > 1)
		{
			//  Prevent from having the same port twice (Note: Since duplicates were prevented here before, there can only ever be one duplicate)
			const int32 LastIndexOfDuplicate = GetLastIndexOfDuplicatePortReference(InputPortReferences);

			const FDMXInputPortSharedRef* AvailablePortPtr = FDMXPortManager::Get().GetInputPorts().FindByPredicate([this, LastIndexOfDuplicate](const FDMXInputPortSharedRef& InputPort) {
				return
					!InputPortReferences.Contains(InputPort->GetPortGuid()) &&
					InputPort->GetPortGuid() != InputPortReferences[LastIndexOfDuplicate].GetPortGuid();
				});

			if (AvailablePortPtr)
			{
				InputPortReferences.EmplaceAt(LastIndexOfDuplicate, FDMXInputPortReference((*AvailablePortPtr)->GetPortGuid()));
			}
			else
			{
				// Don't allow for the same port twice
				InputPortReferences.RemoveAt(LastIndexOfDuplicate);
			}
		}

		UpdatePorts();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXLibrary, OutputPortReferences))
	{
		if(OutputPortReferences.Num() > 1)
		{
			// Prevent from having the same port twice (Note: Since duplicates were prevented here before, there can only ever be one duplicate)
			const int32 LastIndexOfDuplicate = GetLastIndexOfDuplicatePortReference(OutputPortReferences);

			const FDMXOutputPortSharedRef* AvailablePortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([this, LastIndexOfDuplicate](const FDMXOutputPortSharedRef& OutputPort) {
				return
					!OutputPortReferences.Contains(OutputPort->GetPortGuid()) &&
					OutputPort->GetPortGuid() != OutputPortReferences[LastIndexOfDuplicate].GetPortGuid();
				});

			if (AvailablePortPtr)
			{
				OutputPortReferences.EmplaceAt(LastIndexOfDuplicate, FDMXOutputPortReference((*AvailablePortPtr)->GetPortGuid()));
			}
			else
			{
				// Don't allow for the same port twice
				OutputPortReferences.RemoveAt(LastIndexOfDuplicate);
			}
		}

		UpdatePorts();
	}
}
#endif // WITH_EDITOR

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
	AddEntity(Entity);
	OnEntitiesUpdated.Broadcast(this);

	return Entity;
}

UDMXEntity* UDMXLibrary::FindEntity(const FString& InSearchName) const
{
	UDMXEntity*const* Entity = Entities.FindByPredicate([&InSearchName](const UDMXEntity* InEntity)->bool
		{
			return InEntity && InEntity->GetDisplayName().Equals(InSearchName);
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
		if (Entity && Entity->GetID() == Id)
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
	check(InEntity);
	check(!Entities.Contains(InEntity));
	
	Entities.Add(InEntity);
	InEntity->SetParentLibrary(this);

	// Check for unique Id
	for (UDMXEntity* Entity : Entities)
	{
		if (Entity && InEntity->GetID() == Entity->GetID())
		{
			InEntity->RefreshID();
			break;
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
			return Entity && Entity->GetDisplayName().Equals(EntityName);
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
		if (Entity)
		{
			Entity->SetParentLibrary(nullptr);
		}
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
			return Entity && Entity->IsA(InEntityClass);
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

TSet<int32> UDMXLibrary::GetAllLocalUniversesIDsInPorts() const
{
	TSet<int32> Result;
	for (const FDMXPortSharedRef& Port : GenerateAllPortsSet())
	{
		for (int32 UniverseID = Port->GetLocalUniverseStart(); UniverseID <= Port->GetLocalUniverseEnd(); UniverseID++)
		{
			if (!Result.Contains(UniverseID))
			{
				Result.Add(UniverseID);
			}
		}
	}

	return Result;
}

TSet<FDMXPortSharedRef> UDMXLibrary::GenerateAllPortsSet() const
{
	TSet<FDMXPortSharedRef> Result;
	for (const FDMXInputPortSharedRef& InputPort : InputPorts)
	{
		Result.Add(InputPort);
	}

	for (const FDMXOutputPortSharedRef& OutputPort : OutputPorts)
	{
		Result.Add(OutputPort);
	}

	return Result;
}

void UDMXLibrary::UpdatePorts()
{
	// Remove entries no longer present in the port reference arrays
	TSet<FDMXInputPortSharedRef> CachedInputPorts = InputPorts;
	for (const FDMXInputPortSharedRef& InputPort : CachedInputPorts)
	{
		const bool bPortStillExists = InputPortReferences.ContainsByPredicate([&InputPort](const FDMXInputPortReference& InputPortReference) {
			return InputPortReference.GetPortGuid() == InputPort->GetPortGuid();
		});

		if (!bPortStillExists)
		{
			InputPorts.Remove(InputPort);
		}
	}

	TSet<FDMXOutputPortSharedRef> CachedOutputPorts = OutputPorts;
	for (const FDMXOutputPortSharedRef& OutputPort : CachedOutputPorts)
	{
		const bool bPortStillExists = OutputPortReferences.ContainsByPredicate([&OutputPort](const FDMXOutputPortReference& OutputPortReference) {
			return OutputPortReference.GetPortGuid() == OutputPort->GetPortGuid();
			});

		if (!bPortStillExists)
		{
			OutputPorts.Remove(OutputPort);
		}
	}

	// Add entries newly added to the port reference arrays
	for (const FDMXInputPortReference& InputPortRef : InputPortReferences)
	{
		const FGuid& PortGuid = InputPortRef.GetPortGuid();

		for (const FDMXInputPortSharedRef& InputPort : InputPorts)
		{
			if (InputPort->GetPortGuid() == PortGuid)
			{
				continue;
			}
		}

		// No entry found for the current InputPortRef
		const FDMXInputPortSharedRef* InputPortPtr = FDMXPortManager::Get().GetInputPorts().FindByPredicate([&PortGuid](const FDMXInputPortSharedRef& InputPort) {
			return InputPort->GetPortGuid() == PortGuid;
		});

		if (InputPortPtr)
		{
			InputPorts.Add(*InputPortPtr);
		}
		else
		{
			// We do not expect the port to be missing, as we warned the user when trying to remove a port from settings while it still is referenced 
			UE_LOG(LogDMXRuntime, Warning, TEXT("DMX Library %s contains an invalid port. Please update the library."), *GetName());
		}
	}

	for (const FDMXOutputPortReference& OutputPortRef : OutputPortReferences)
	{
		const FGuid& PortGuid = OutputPortRef.GetPortGuid();

		for (const FDMXOutputPortSharedRef& OutputPort : OutputPorts)
		{
			if (OutputPort->GetPortGuid() == PortGuid)
			{
				continue;
			}
		}

		// No entry found for the current OutputPortRef
		const FDMXOutputPortSharedRef* OutputPortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([&PortGuid](const FDMXOutputPortSharedRef& OutputPort) {
			return OutputPort->GetPortGuid() == PortGuid;
			});

		if (OutputPortPtr)
		{
			OutputPorts.Add(*OutputPortPtr);
		}
		else
		{
			// We do not expect the port to be missing, as we warned the user when trying to remove a port from settings while it still is referenced 
			UE_LOG(LogDMXRuntime, Warning, TEXT("DMX Library %s contains an invalid port. Please update the library."), *GetName());
		}
	}

	OnPortsChanged.Broadcast();
}

#undef LOCTEXT_NAMESPACE
