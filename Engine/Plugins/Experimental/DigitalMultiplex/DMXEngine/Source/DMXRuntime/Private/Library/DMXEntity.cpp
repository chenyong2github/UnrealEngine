// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntity.h"
#include "Library/DMXLibrary.h"
#include "Interfaces/IDMXProtocol.h"

#include "Dom/JsonObject.h"

UDMXEntity::UDMXEntity()
	: ParentLibrary(nullptr)
{
	FPlatformMisc::CreateGuid(Id);
}

FString UDMXEntity::GetDisplayName() const
{
	return Name;
}

void UDMXEntity::SetName(const FString& InNewName)
{
	// TODO check name for validity
	Name = InNewName;
}

void UDMXEntity::SetParentLibrary(UDMXLibrary* InParent)
{
	ParentLibrary = InParent;
}

void UDMXEntity::RefreshID()
{
	FPlatformMisc::CreateGuid(Id);
}

void UDMXEntity::ReplicateID(UDMXEntity* Other)
{
	Id = Other->Id;
}

#if WITH_EDITOR
void UDMXEntity::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	RefreshID();
}
#endif // WITH_EDITOR

UDMXEntityUniverseManaged::UDMXEntityUniverseManaged()
{
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		DeviceProtocol = FDMXProtocolName(IDMXProtocol::GetFirstProtocolName());
	}
}

void UDMXEntityUniverseManaged::PostLoad()
{
	Super::PostLoad();
	UpdateProtocolUniverses();
}

#if WITH_EDITOR

void UDMXEntityUniverseManaged::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	UpdateProtocolUniverses();
}

#endif // WITH_EDITOR

void UDMXEntityUniverseManaged::UpdateProtocolUniverses() const
{
	if (DeviceProtocol.IsValid())
	{
		TSharedPtr<IDMXProtocol> DMXProtocol = DeviceProtocol;
		DMXProtocol->CollectUniverses(Universes);
	}
}
