// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXLibrary.h"

#include "DMXRuntimeLog.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolUtils.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXPortManager.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixturePatch.h"


#define LOCTEXT_NAMESPACE "DMXLibrary"

UDMXLibrary::UDMXLibrary()
{
	// Bind to port changes and update ports (for new libraries)
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
		ProtocolSettings->OnPortConfigsChanged.AddUObject(this, &UDMXLibrary::UpdatePorts);

		UpdatePorts();
	}
}

void UDMXLibrary::PostLoad()
{
	Super::PostLoad();

	// Bind to port changes and update ports (for loaded libraries)
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
		if (!ProtocolSettings->OnPortConfigsChanged.IsBoundToObject(this))
		{
			ProtocolSettings->OnPortConfigsChanged.AddUObject(this, &UDMXLibrary::UpdatePorts);
		}

		UpdatePorts();

#if WITH_EDITOR
		bool bNeedsUpgradeFromControllersToPorts = Entities.ContainsByPredicate([](UDMXEntity* Entity) {
			return Cast<UDMXEntityController>(Entity) != nullptr;
			});
		if (bNeedsUpgradeFromControllersToPorts)
		{
			UpgradeFromControllersToPorts();
		}
#endif 
	}

	// Sanetize
	TArray<UDMXEntity*> CachedEntities = Entities;
	for (UDMXEntity* Entity : CachedEntities)
	{
		// From hereon all entities have to be valid. We should never enter this statement.
		if (!ensure(Entity))
		{
			UE_LOG(LogDMXRuntime, Warning, TEXT("Invalid Entity found in Library %s. Please resave the library."), *GetName());
			Modify();
			Entities.Remove(Entity);
		}
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

	// Update the ports 
	UpdatePorts();
}

#if WITH_EDITOR
void UDMXLibrary::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == FDMXInputPortReference::GetEnabledFlagPropertyName() ||
		PropertyName == FDMXOutputPortReference::GetEnabledFlagPropertyName())
	{
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
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	check(ProtocolSettings);

	// Remove ports refs that don't exist anymore
	PortReferences.InputPortReferences.RemoveAll([ProtocolSettings](const FDMXInputPortReference& InputPortReference) {
		const FGuid& PortGuid = InputPortReference.GetPortGuid();

		bool bPortExists = ProtocolSettings->InputPortConfigs.ContainsByPredicate([PortGuid](const FDMXInputPortConfig & InputPortConfig) {
			return InputPortConfig.GetPortGuid() == PortGuid;
		});

		return !bPortExists;
	});

	PortReferences.OutputPortReferences.RemoveAll([ProtocolSettings](const FDMXOutputPortReference& OutputPortReference) {
		const FGuid& PortGuid = OutputPortReference.GetPortGuid();

		bool bPortExists = ProtocolSettings->OutputPortConfigs.ContainsByPredicate([PortGuid](const FDMXOutputPortConfig& OutputPortConfig) {
			return OutputPortConfig.GetPortGuid() == PortGuid;
		});

		return !bPortExists;
	});

	// Add port refs from newly created ports
	for (int32 IndexInputPortConfig = 0; IndexInputPortConfig < ProtocolSettings->InputPortConfigs.Num(); IndexInputPortConfig++)
	{
		const FGuid& InputPortGuid = ProtocolSettings->InputPortConfigs[IndexInputPortConfig].GetPortGuid();

		bool bInputPortExists = PortReferences.InputPortReferences.ContainsByPredicate([InputPortGuid](const FDMXInputPortReference& InputPortReference) {
			return InputPortReference.GetPortGuid() == InputPortGuid;
		});

		if (!bInputPortExists)
		{
			// Default to disabled
			bool bEnabled = false;
			PortReferences.InputPortReferences.Insert(FDMXInputPortReference(InputPortGuid, bEnabled), IndexInputPortConfig);
		}
	}

	for (int32 IndexOutputPortConfig = 0; IndexOutputPortConfig < ProtocolSettings->OutputPortConfigs.Num(); IndexOutputPortConfig++)
	{
		const FGuid& OutputPortGuid = ProtocolSettings->OutputPortConfigs[IndexOutputPortConfig].GetPortGuid();

		bool bOutputPortExists = PortReferences.OutputPortReferences.ContainsByPredicate([OutputPortGuid](const FDMXOutputPortReference& OutputPortReference) {
			return OutputPortReference.GetPortGuid() == OutputPortGuid;
			});

		if (!bOutputPortExists)
		{
			// Default to disabled
			bool bEnabled = false;
			PortReferences.OutputPortReferences.Insert(FDMXOutputPortReference(OutputPortGuid, bEnabled), IndexOutputPortConfig);
		}
	}

	// Rebuild the arrays of actual ports
	InputPorts.Reset();
	OutputPorts.Reset();

	for (const FDMXInputPortReference& InputPortReference : PortReferences.InputPortReferences)
	{
		if (InputPortReference.IsEnabledFlagSet())
		{
			const FGuid& PortGuid = InputPortReference.GetPortGuid();
			FDMXInputPortSharedPtr InputPort = FDMXPortManager::Get().FindInputPortByGuid(PortGuid); 

			if (InputPort.IsValid())
			{
				InputPorts.Add(InputPort.ToSharedRef());
			}
		}
	}

	for (const FDMXOutputPortReference& OutputPortReference : PortReferences.OutputPortReferences)
	{
		if (OutputPortReference.IsEnabledFlagSet())
		{
			const FGuid& PortGuid = OutputPortReference.GetPortGuid();
			FDMXOutputPortSharedPtr OutputPort = FDMXPortManager::Get().FindOutputPortByGuid(PortGuid);

			if (OutputPort)
			{
				OutputPorts.Add(OutputPort.ToSharedRef());
			}
		}
	}
}

#if WITH_EDITOR
void UDMXLibrary::UpgradeFromControllersToPorts()
{	
	// This function only needs be called to upgrade projects created before 4.27

	// Only continue if an upgrade may be required
	bool bNeedsUpgradeFromControllersToPorts = Entities.ContainsByPredicate([](UDMXEntity* Entity) {
		return Cast<UDMXEntityController>(Entity) != nullptr;
	});

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (bNeedsUpgradeFromControllersToPorts)
	{
		UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();

		// Disable all port references, to later only enable the ones that got generated from controllers
		for (FDMXInputPortReference& InputPortRef : PortReferences.InputPortReferences)
		{
			bool bEnabledFlag = false;
			InputPortRef = FDMXInputPortReference(InputPortRef, bEnabledFlag);
		}

		// Disable all port references, to later only enable the ones that got generated from controllers
		for (FDMXOutputPortReference& OutputPortRef : PortReferences.OutputPortReferences)
		{
			bool bEnabledFlag = false;
			OutputPortRef = FDMXOutputPortReference(OutputPortRef, bEnabledFlag);
		}

		// Helpers to enable port references
		struct Local
		{
			static void EnableInputPortRef(UDMXLibrary* ThisLibrary, const FGuid& PortGuid)
			{
				ThisLibrary->UpdatePorts();

				FDMXInputPortReference* ExistingPortRef = ThisLibrary->PortReferences.InputPortReferences.FindByPredicate([PortGuid](const FDMXInputPortReference& InputPortRef) {
					return InputPortRef.GetPortGuid() == PortGuid;
					});

				*ExistingPortRef = FDMXInputPortReference(PortGuid, true);

				FDMXPortManager::Get().UpdateFromProtocolSettings();
			}

			static void EnableOutputPortRef(UDMXLibrary* ThisLibrary, const FGuid& PortGuid)
			{
				ThisLibrary->UpdatePorts();

				FDMXOutputPortReference* ExistingPortRef = ThisLibrary->PortReferences.OutputPortReferences.FindByPredicate([PortGuid](const FDMXOutputPortReference& OutputPortRef) {
					return OutputPortRef.GetPortGuid() == PortGuid;
					});

				*ExistingPortRef = FDMXOutputPortReference(PortGuid, true);

				FDMXPortManager::Get().UpdateFromProtocolSettings();
			}
		};

		TArray<UDMXEntity*> CachedEntities = Entities;
		for (UDMXEntity* Entity : CachedEntities)
		{
			// Clean out controllers that were in the entity array before 4.27
			// Create a corresponding port in project settings, enable only these in the Library.
			if (UDMXEntityController* VoidController = Cast<UDMXEntityController>(Entity))
			{
				Modify();
				ProtocolSettings->Modify();

				// Remove the controller from the library
				Entities.Remove(VoidController);

				// Find the best NetworkInterface IP
				FString InterfaceIPAddress_DEPRECATED = ProtocolSettings->InterfaceIPAddress_DEPRECATED;
				if (InterfaceIPAddress_DEPRECATED.IsEmpty())
				{
					TArray<TSharedPtr<FString>> LocalNetworkInterfaceCardIPs = FDMXProtocolUtils::GetLocalNetworkInterfaceCardIPs();
					if (LocalNetworkInterfaceCardIPs.Num() > 0)
					{
						InterfaceIPAddress_DEPRECATED = *LocalNetworkInterfaceCardIPs[0];
					}
				}

				FName ProtocolName = VoidController->DeviceProtocol;
				int32 GlobalUniverseOffset_DEPRECATED = ProtocolName == "Art-Net" ? ProtocolSettings->GlobalArtNetUniverseOffset_DEPRECATED : ProtocolSettings->GlobalSACNUniverseOffset_DEPRECATED;

				// Cache names of all port config to generate new port names
				TSet<FString> InputPortConfigNames;
				for (const FDMXInputPortConfig& InputPortConfig : ProtocolSettings->InputPortConfigs)
				{
					InputPortConfigNames.Add(InputPortConfig.PortName);
				}

				TSet<FString> OutputPortConfigNames;
				for (const FDMXOutputPortConfig& OutputPortConfig : ProtocolSettings->OutputPortConfigs)
				{
					OutputPortConfigNames.Add(OutputPortConfig.PortName);
				}

				// Convert the controller to an input port	
				FDMXInputPortConfig* ExistingInputPortConfigPtr = ProtocolSettings->FindInputPortConfig([VoidController, &InterfaceIPAddress_DEPRECATED, &ProtocolName](FDMXInputPortConfig& InputPortConfig) {
					return
						InputPortConfig.DeviceAddress == InterfaceIPAddress_DEPRECATED &&
						InputPortConfig.ProtocolName == ProtocolName;
				});

				if (ExistingInputPortConfigPtr)
				{
					EDMXCommunicationType CommunicationType = EDMXCommunicationType::InternalOnly;
					ExistingInputPortConfigPtr->CommunicationType = CommunicationType;

					int32 LocalUniverseStart = FMath::Min(ExistingInputPortConfigPtr->LocalUniverseStart, VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED);
					ExistingInputPortConfigPtr->LocalUniverseStart = LocalUniverseStart;

					int32 NumUniverses = FMath::Max(ExistingInputPortConfigPtr->NumUniverses, VoidController->UniverseLocalEnd - VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED + 1);
					ExistingInputPortConfigPtr->NumUniverses = NumUniverses;

					int32 ExternUniverseStart = FMath::Max(ExistingInputPortConfigPtr->ExternUniverseStart, VoidController->UniverseRemoteStart + GlobalUniverseOffset_DEPRECATED);
					ExistingInputPortConfigPtr->ExternUniverseStart = ExternUniverseStart;

					Local::EnableInputPortRef(this, ExistingInputPortConfigPtr->GetPortGuid());
				}
				else if (IDMXProtocol::Get(ProtocolName).IsValid())
				{
					FDMXInputPortConfig InputPortConfig = FDMXInputPortConfig(FGuid::NewGuid());

					InputPortConfig.ProtocolName = ProtocolName;
					InputPortConfig.DeviceAddress = InterfaceIPAddress_DEPRECATED;
					InputPortConfig.PortName = FDMXProtocolUtils::GenerateUniqueNameFromExisting(InputPortConfigNames, TEXT("Generated_InputPort"));
					InputPortConfig.CommunicationType = EDMXCommunicationType::InternalOnly;
					InputPortConfig.LocalUniverseStart = VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED;
					InputPortConfig.NumUniverses = VoidController->UniverseLocalEnd - VoidController->UniverseLocalStart + 1;
					InputPortConfig.ExternUniverseStart = VoidController->UniverseRemoteStart + GlobalUniverseOffset_DEPRECATED;

					FDMXInputPortSharedRef InputPort = FDMXPortManager::Get().GetOrCreateInputPortFromConfig(InputPortConfig);

					ProtocolSettings->InputPortConfigs.Add(InputPortConfig);

					Local::EnableInputPortRef(this, InputPortConfig.GetPortGuid());
				}

				// Convert the controller to output ports

				// Add a port for the default output
				FDMXOutputPortConfig* ExistingOutputPortConfigPtr = ProtocolSettings->FindOutputPortConfig([VoidController, &InterfaceIPAddress_DEPRECATED, &ProtocolName](FDMXOutputPortConfig& OutputPortConfig) {
					return
						OutputPortConfig.DeviceAddress == InterfaceIPAddress_DEPRECATED &&
						OutputPortConfig.ProtocolName == ProtocolName &&
						OutputPortConfig.CommunicationType == VoidController->CommunicationMode;
				});

				if (ExistingOutputPortConfigPtr)
				{
					ExistingOutputPortConfigPtr->CommunicationType = VoidController->CommunicationMode;

					int32 LocalUniverseStart = FMath::Min(ExistingOutputPortConfigPtr->LocalUniverseStart, VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED);
					ExistingOutputPortConfigPtr->LocalUniverseStart = LocalUniverseStart;

					int32 NumUniverses = FMath::Max(ExistingOutputPortConfigPtr->NumUniverses, VoidController->UniverseLocalEnd - VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED + 1);
					ExistingOutputPortConfigPtr->NumUniverses = NumUniverses;

					int32 ExternUniverseStart = FMath::Max(ExistingOutputPortConfigPtr->ExternUniverseStart, VoidController->UniverseRemoteStart + GlobalUniverseOffset_DEPRECATED);
					ExistingOutputPortConfigPtr->ExternUniverseStart = ExternUniverseStart;

					ExistingOutputPortConfigPtr->bLoopbackToEngine = VoidController->CommunicationMode != EDMXCommunicationType::Broadcast;

					Local::EnableOutputPortRef(this, ExistingOutputPortConfigPtr->GetPortGuid());
				}
				else if (IDMXProtocol::Get(ProtocolName).IsValid())
				{
					FDMXOutputPortConfig OutputPortConfig = FDMXOutputPortConfig(FGuid::NewGuid());

					OutputPortConfig.ProtocolName = ProtocolName;
					OutputPortConfig.DeviceAddress = InterfaceIPAddress_DEPRECATED;
					OutputPortConfig.PortName = FDMXProtocolUtils::GenerateUniqueNameFromExisting(OutputPortConfigNames, TEXT("Generated_OutputPort"));
					OutputPortConfig.CommunicationType = VoidController->CommunicationMode;
					OutputPortConfig.LocalUniverseStart = VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED;
					OutputPortConfig.NumUniverses = VoidController->UniverseLocalEnd - VoidController->UniverseLocalStart + 1;
					OutputPortConfig.ExternUniverseStart = VoidController->UniverseRemoteStart + GlobalUniverseOffset_DEPRECATED;
					OutputPortConfig.bLoopbackToEngine = VoidController->CommunicationMode != EDMXCommunicationType::Broadcast;

					FDMXOutputPortSharedRef OutputPort = FDMXPortManager::Get().GetOrCreateOutputPortFromConfig(OutputPortConfig);

					ProtocolSettings->OutputPortConfigs.Add(OutputPortConfig);

					Local::EnableOutputPortRef(this, OutputPortConfig.GetPortGuid());
				}

				// Add ports from additional unicast ip
				for (const FString& AdditionalUnicastIP : VoidController->AdditionalUnicastIPs)
				{
					ExistingOutputPortConfigPtr = ProtocolSettings->FindOutputPortConfig([VoidController, &InterfaceIPAddress_DEPRECATED, &ProtocolName](FDMXOutputPortConfig& OutputPortConfig) {
						return
							OutputPortConfig.DeviceAddress == InterfaceIPAddress_DEPRECATED &&
							OutputPortConfig.ProtocolName == ProtocolName &&
							OutputPortConfig.CommunicationType == EDMXCommunicationType::Unicast;
					});

					if (ExistingOutputPortConfigPtr)
					{
						ExistingOutputPortConfigPtr->CommunicationType = EDMXCommunicationType::Unicast;

						int32 LocalUniverseStart = FMath::Min(ExistingOutputPortConfigPtr->LocalUniverseStart, VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED);
						ExistingOutputPortConfigPtr->LocalUniverseStart = LocalUniverseStart;

						int32 NumUniverses = FMath::Max(ExistingOutputPortConfigPtr->NumUniverses, VoidController->UniverseLocalEnd - VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED + 1);
						ExistingOutputPortConfigPtr->NumUniverses = NumUniverses;

						int32 ExternUniverseStart = FMath::Max(ExistingOutputPortConfigPtr->ExternUniverseStart, VoidController->UniverseRemoteStart + GlobalUniverseOffset_DEPRECATED);
						ExistingOutputPortConfigPtr->ExternUniverseStart = ExternUniverseStart;

						ExistingOutputPortConfigPtr->bLoopbackToEngine = false;

						Local::EnableOutputPortRef(this, ExistingOutputPortConfigPtr->GetPortGuid());
					}
					else if (IDMXProtocol::Get(ProtocolName).IsValid())
					{
						FDMXOutputPortConfig OutputPortConfig = FDMXOutputPortConfig(FGuid::NewGuid());

						OutputPortConfig.ProtocolName = ProtocolName;
						OutputPortConfig.DeviceAddress = InterfaceIPAddress_DEPRECATED;
						OutputPortConfig.PortName = FDMXProtocolUtils::GenerateUniqueNameFromExisting(OutputPortConfigNames, TEXT("Generated_OutputPort"));
						OutputPortConfig.CommunicationType = EDMXCommunicationType::Unicast;
						OutputPortConfig.LocalUniverseStart = VoidController->UniverseLocalStart + GlobalUniverseOffset_DEPRECATED;
						OutputPortConfig.NumUniverses = VoidController->UniverseLocalEnd - VoidController->UniverseLocalStart + 1;
						OutputPortConfig.ExternUniverseStart = VoidController->UniverseRemoteStart + GlobalUniverseOffset_DEPRECATED;
						OutputPortConfig.bLoopbackToEngine = false;

						FDMXOutputPortSharedRef OutputPort = FDMXPortManager::Get().GetOrCreateOutputPortFromConfig(OutputPortConfig);

						ProtocolSettings->OutputPortConfigs.Add(OutputPortConfig);

						Local::EnableOutputPortRef(this, OutputPortConfig.GetPortGuid());
					}
				}
			}
		}

		UpdatePorts();
	}

	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
