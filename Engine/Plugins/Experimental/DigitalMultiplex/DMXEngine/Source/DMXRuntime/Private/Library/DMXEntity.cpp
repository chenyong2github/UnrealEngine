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
void UDMXEntityUniverseManaged::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_Universes = GET_MEMBER_NAME_CHECKED(UDMXEntityUniverseManaged, Universes);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXUniverse, UniverseNumber) 
		|| PropertyChangedEvent.GetPropertyName() == NAME_Universes
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityUniverseManaged, DeviceProtocol))
	{
		// Keep the Universe ID values within the valid range for the current protocol
		if (DeviceProtocol.IsValid())
		{
			const IDMXProtocolPtr Protocol = DeviceProtocol.GetProtocol();
			const uint32 MinUniverseID = Protocol->GetMinUniverseID();
			const uint32 MaxUniverseID = Protocol->GetMaxUniverses();

			for (FDMXUniverse& Universe : Universes)
			{
				Universe.UniverseNumber = FMath::Clamp(Universe.UniverseNumber, MinUniverseID, MaxUniverseID);
			}
		}

		// New Universes will have their directionality set to Output
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd
			&& PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityUniverseManaged, Universes))
		{
			Universes.Last(0).DMXProtocolDirectionality = EDMXProtocolDirectionality::EOutput;
		}
	}

	UpdateProtocolUniverses();
}

#endif // WITH_EDITOR

void UDMXEntityUniverseManaged::UpdateProtocolUniverses() const
{
	if (DeviceProtocol.IsValid())
	{
		IDMXProtocolPtr DMXProtocol = DeviceProtocol;
		DMXProtocol->CollectUniverses(Universes);
	}
}
